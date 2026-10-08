#include "model.h"
#include "json_wire.h"
#include "rotation_runtime.h"
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
bool rotationStaged = false;
ConfigStorageState storageState = ConfigStorageState::Missing;

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
  if (doc.overflowed() || measureSettingsJson(doc) > MAX_RECORD_BYTES) { error = "Record memory/size limit"; return false; }
  auto file = LittleFS.open(STAGED, mode);
  if (!file) return false;
  const auto expected = measureSettingsJson(doc);
  bool ok = serializeSettingsJson(doc, file) == expected && file.write(uint8_t('\n')) == 1;
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
  auto& config = activeConfig();
  error = "Invalid LittleFS settings records";
  auto file = LittleFS.open(path, "r");
  if (!file) return false;
  JsonDocument doc; NetworkSettings network;
  if (!readRecord(file, doc) || !doc["storageVersion"].is<unsigned>() || doc["storageVersion"] != 2) return false;
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
  scratch.reset(); // Chain and Rotation scratch buffers never overlap.
  std::unique_ptr<EncoderRotationSettings> rotation(new (std::nothrow) EncoderRotationSettings);
  if (!rotation) { error = "Not enough RAM for rotation read"; return false; }
  if (!readRecord(file, doc) || !decodeRotation(doc.as<JsonVariantConst>(), *rotation, error)) return false;
  if (apply) config.encoderRotation = *rotation;
  if (file.read() != -1) { error = "Trailing settings data"; return false; }
  error = ""; return true;
}
void defaults() {
  auto& config = activeConfig();
  static_cast<NetworkSettings&>(config) = NetworkSettings{};
  for (auto& chain : config.chains) chain.count = 0;
  config.encoderRotation.mode = RotationMode::ActionChain;
  config.encoderRotation.axis = RotationAxis{};
  config.encoderRotation.outputCount = 0;
}
ConfigStorageState inspect(String& error) {
  if (!mount(error)) return ConfigStorageState::IoError;
  if (!LittleFS.exists(ACTIVE)) return ConfigStorageState::Missing;
  auto file = LittleFS.open(ACTIVE, "r");
  if (!file) { error = "Settings open failed"; return ConfigStorageState::IoError; }
  JsonDocument doc;
  if (!readRecord(file, doc) || !doc["storageVersion"].is<unsigned>()) {
    error = "Corrupt settings header"; return ConfigStorageState::Corrupt;
  }
  if (doc["storageVersion"] != 2) {
    error = "Unsupported storageVersion: existing settings preserved; explicit v2 replacement required";
    return ConfigStorageState::UnsupportedVersion;
  }
  return ConfigStorageState::Ready;
}
}
bool configStorageMounted() { return mounted; }
ConfigStorageState configStorageState() { return storageState; }
const char* configStorageStateName() {
  const char* names[] = {"missing", "ready", "unsupportedVersion", "corrupt", "ioError"};
  return names[unsigned(storageState)];
}
bool configOutputsAllowed() { return storageState == ConfigStorageState::Ready || storageState == ConfigStorageState::Missing; }
uint32_t beginConfigSave(const String& json, String& error) {
  transaction = 0; rotationStaged = false;
  if (!mount(error)) return 0;
  if (json.length() > 2048) { error = "Network request too large"; return 0; }
  JsonDocument doc; NetworkSettings network;
  if (deserializeJson(doc, json)) { error = "Invalid network JSON"; return 0; }
  if (!doc["schemaVersion"].is<unsigned>() || doc["schemaVersion"] != 2) { error = "Unsupported schemaVersion (expected 2)"; return 0; }
  if (!decodeNetwork(doc["network"], network, error)) return 0;
  const auto existing = inspect(error);
  if (existing == ConfigStorageState::IoError || existing == ConfigStorageState::Corrupt) return 0;
  if (existing == ConfigStorageState::UnsupportedVersion &&
      (!doc["replaceUnsupported"].is<bool>() || !doc["replaceUnsupported"].as<bool>())) return 0;
  // A new begin invalidates any abandoned transaction; its active file is untouched.
  transaction = 0; nextChain = 0; stagedPressCount = 0;
  encodeNetwork(network, doc);
  JsonDocument header; header["storageVersion"] = 2; header["network"] = doc;
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
  auto& config = activeConfig();
  error = "Incomplete or expired configuration transaction";
  if (!transaction || token != transaction || nextChain != INPUT_COUNT || !rotationStaged) return false;
  transaction = 0;
  if (!scan(STAGED, false, error)) return false;
  // LittleFS rename-over-existing is atomic, including across power loss.
  if (!LittleFS.rename(STAGED, ACTIVE)) { error = "LittleFS commit failed"; return false; }
  if (!scan(ACTIVE, true, error)) { defaults(); storageState = ConfigStorageState::IoError; rotationRuntime.apply(config.encoderRotation, false); error = "Committed settings read failed; restart required"; return false; }
  storageState = ConfigStorageState::Ready;
  rotationRuntime.apply(config.encoderRotation);
  error = ""; return true;
}
bool loadConfig(String& message) {
  auto& config = activeConfig();
  transaction = 0;
  defaults();
  rotationRuntime.restart(config.encoderRotation, false);
  storageState = inspect(message);
  if (storageState == ConfigStorageState::Missing) { message = "Factory config: all chains empty"; return true; }
  if (storageState != ConfigStorageState::Ready) return false;
  if (!scan(ACTIVE, false, message) || !scan(ACTIVE, true, message)) { defaults(); storageState = ConfigStorageState::Corrupt; return false; }
  rotationRuntime.restart(config.encoderRotation);
  message = "Loaded LittleFS settings"; return true;
}
bool saveWifiConfig(const String& json, String& error) {
  auto& config = activeConfig();
  if (!configOutputsAllowed()) { error = "Settings unavailable: Wi-Fi save cannot replace existing settings"; return false; }
  if (json.length() > 1024) { error = "Wi-Fi request too large"; return false; }
  JsonDocument credentials, doc;
  if (deserializeJson(credentials, json)) { error = "Invalid Wi-Fi JSON"; return false; }
  encodeNetwork(config, doc);
  doc["ssid"] = credentials["ssid"]; doc["password"] = credentials["password"];
  JsonDocument request; request["schemaVersion"] = 2; request["network"] = doc;
  String body; serializeJson(request, body); request.clear();
  credentials.clear(); doc.clear();
  auto token = beginConfigSave(body, error);
  if (!token) return false;
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    encodeChain(config.chains[i], i, doc); body = ""; serializeJson(doc, body); doc.clear();
    if (!stageConfigChain(token, i, body, error)) return false;
  }
  encodeRotation(config.encoderRotation, doc, true); body = ""; serializeJson(doc, body); doc.clear();
  if (!stageConfigRotation(token, body, error)) return false;
  return commitConfigSave(token, error);
}
bool stageConfigRotation(uint32_t token, const String& json, String& error) {
  error = "Invalid or expired rotation transaction";
  if (!transaction || token != transaction || nextChain != INPUT_COUNT || rotationStaged) return false;
  if (json.length() > MAX_RECORD_BYTES) { transaction = 0; error = "Rotation request too large"; return false; }
  JsonDocument doc;
  std::unique_ptr<EncoderRotationSettings> rotation(new (std::nothrow) EncoderRotationSettings);
  if (!rotation) { transaction = 0; error = "Not enough RAM for rotation stage"; return false; }
  if (deserializeJson(doc, json) || !decodeRotation(doc.as<JsonVariantConst>(), *rotation, error)) { transaction = 0; return false; }
  encodeRotation(*rotation, doc, true);
  if (!writeRecord(doc, "a", error)) { transaction = 0; return false; }
  rotationStaged = true; error = ""; return true;
}
} // namespace chimera
