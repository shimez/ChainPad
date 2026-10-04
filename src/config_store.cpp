#include "model.h"
#include <LittleFS.h>
#include <memory>
#include <new>
#ifndef CHAINPAD_HOST_TEST
#include <esp_partition.h>
#endif

namespace chimera {
namespace {
constexpr char ACTIVE[] = "/config.records";
constexpr char STAGED[] = "/pending.records";
bool mounted = false;
uint32_t generation = 0, transaction = 0;
uint8_t nextChain = 0;
uint8_t stagedPressCount = 0;

bool mount(String& error) {
  if (mounted) return true;
  mounted = LittleFS.begin(false, "/littlefs", 3, "settings");
#ifndef CHAINPAD_HOST_TEST
  if (!mounted) {
    // Only a completely erased NEW filesystem partition may be formatted.
    // A damaged filesystem must not silently erase the user's saved settings.
    auto p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "settings");
    if (p) {
      uint8_t buffer[256]; bool erased = true;
      for (size_t offset = 0; erased && offset < p->size; offset += sizeof(buffer)) {
        if (esp_partition_read(p, offset, buffer, sizeof(buffer)) != ESP_OK) { erased = false; break; }
        for (auto b : buffer) if (b != 0xff) { erased = false; break; }
      }
      if (erased) mounted = LittleFS.begin(true, "/littlefs", 3, "settings");
    }
  }
#endif
  if (!mounted) error = "LittleFS mount failed";
  return mounted;
}
bool writeRecord(JsonDocument& doc, const char* mode, String& error) {
  error = "LittleFS write failed";
  if (doc.overflowed() || measureJson(doc) > MAX_RECORD_BYTES) { error = "Record memory/size limit"; return false; }
  auto file = LittleFS.open(STAGED, mode);
  if (!file) return false;
  const auto expected = measureJson(doc);
  bool ok = serializeJson(doc, file) == expected && file.write(uint8_t('\n')) == 1;
  file.flush(); file.close();
  return ok;
}
// A bounded reader keeps corrupt files from causing unbounded JSON allocations.
struct RecordReader {
  File& file; size_t remaining = MAX_RECORD_BYTES;
  int read() { if (!remaining) return -1; --remaining; return file.read(); }
  size_t readBytes(char* dst, size_t n) {
    size_t i = 0; for (; i < n; ++i) { int c = read(); if (c < 0) break; dst[i] = char(c); } return i;
  }
};
bool readRecord(File& file, JsonDocument& doc) {
  RecordReader reader{file};
  return !deserializeJson(doc, reader, DeserializationOption::NestingLimit(8)) && file.read() == '\n';
}
bool scan(const char* path, bool apply, String& error) {
  error = "Invalid LittleFS settings records";
  auto file = LittleFS.open(path, "r");
  if (!file) return false;
  JsonDocument doc; NetworkSettings network;
  if (!readRecord(file, doc) || doc["storageVersion"] != 1) return false;
  if (!decodeNetwork(doc["network"], network, error)) return false;
  // Validation pass completes before any runtime setting is changed.
  if (apply) static_cast<NetworkSettings&>(config) = network;
  std::unique_ptr<Chain> scratch(new (std::nothrow) Chain);
  if (!scratch) { error = "Not enough RAM for chain read"; return false; }
  auto& chain = *scratch; uint8_t pressCount = 0;
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    if (!readRecord(file, doc)) { error = "Invalid LittleFS chain record"; return false; }
    if (!decodeChain(doc.as<JsonVariantConst>(), i, chain, error)) return false;
    if (i < 26) {
      if (!(i % 2)) pressCount = chain.count;
      else if (pressCount + chain.count > MAX_KEY_ACTIONS) { error = "Press / Release total exceeds 16 Actions"; return false; }
    }
    if (apply) config.chains[i].assign(chain);
  }
  if (file.read() != -1) { error = "Trailing settings data"; return false; }
  error = ""; return true;
}
void defaults() {
  static_cast<NetworkSettings&>(config) = NetworkSettings{};
  for (auto& chain : config.chains) chain.count = 0;
}
}
bool configStorageMounted() { return mounted; }
uint32_t beginConfigSave(const String& json, String& error) {
  if (!mount(error)) return 0;
  if (json.length() > 2048) { error = "Network request too large"; return 0; }
  JsonDocument doc; NetworkSettings network;
  if (deserializeJson(doc, json)) { error = "Invalid network JSON"; return 0; }
  if (!decodeNetwork(doc.as<JsonVariantConst>(), network, error)) return 0;
  // A new begin invalidates any abandoned transaction; its active file is untouched.
  transaction = 0; nextChain = 0; stagedPressCount = 0;
  encodeNetwork(network, doc);
  JsonDocument header; header["storageVersion"] = 1; header["network"] = doc;
  if (!writeRecord(header, "w", error)) return 0;
  if (++generation == 0) ++generation;
  transaction = generation; error = ""; return transaction;
}
bool stageConfigChain(uint32_t token, uint8_t id, const String& json, String& error) {
  error = "Invalid or expired configuration transaction";
  if (!transaction || token != transaction || id != nextChain || id >= INPUT_COUNT) return false;
  if (json.length() > MAX_RECORD_BYTES) { error = "Chain request too large"; transaction = 0; return false; }
  JsonDocument doc;
  std::unique_ptr<Chain> scratch(new (std::nothrow) Chain);
  if (!scratch) { error = "Not enough RAM for chain stage"; transaction = 0; return false; }
  auto& chain = *scratch;
  if (deserializeJson(doc, json)) { error = "Invalid chain JSON"; transaction = 0; return false; }
  if (!decodeChain(doc.as<JsonVariantConst>(), id, chain, error)) { transaction = 0; return false; }
  if (id < 26) {
    if (!(id % 2)) stagedPressCount = chain.count;
    else if (stagedPressCount + chain.count > MAX_KEY_ACTIONS) {
      error = "Press / Release total exceeds 16 Actions"; transaction = 0; return false;
    }
  }
  encodeChain(chain, id, doc);
  if (!writeRecord(doc, "a", error)) { transaction = 0; return false; }
  ++nextChain; error = ""; return true;
}
bool commitConfigSave(uint32_t token, String& error) {
  error = "Incomplete or expired configuration transaction";
  if (!transaction || token != transaction || nextChain != INPUT_COUNT) return false;
  transaction = 0;
  if (!scan(STAGED, false, error)) return false;
  // LittleFS rename-over-existing is atomic, including across power loss.
  if (!LittleFS.rename(STAGED, ACTIVE)) { error = "LittleFS commit failed"; return false; }
  if (!scan(ACTIVE, true, error)) { defaults(); error = "Committed settings read failed; restart required"; return false; }
  error = ""; return true;
}
bool loadConfig(String& message) {
  transaction = 0;
  defaults();
  if (!mount(message)) return false;
  if (!LittleFS.exists(ACTIVE)) { message = "Factory config: all chains empty"; return true; }
  if (!scan(ACTIVE, false, message) || !scan(ACTIVE, true, message)) { defaults(); return false; }
  message = "Loaded LittleFS settings"; return true;
}
bool saveWifiConfig(const String& json, String& error) {
  if (json.length() > 1024) { error = "Wi-Fi request too large"; return false; }
  JsonDocument credentials, doc;
  if (deserializeJson(credentials, json)) { error = "Invalid Wi-Fi JSON"; return false; }
  encodeNetwork(config, doc);
  doc["ssid"] = credentials["ssid"]; doc["password"] = credentials["password"];
  String body; serializeJson(doc, body);
  credentials.clear(); doc.clear();
  auto token = beginConfigSave(body, error);
  if (!token) return false;
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    encodeChain(config.chains[i], i, doc); body = ""; serializeJson(doc, body); doc.clear();
    if (!stageConfigChain(token, i, body, error)) return false;
  }
  return commitConfigSave(token, error);
}
} // namespace chimera
