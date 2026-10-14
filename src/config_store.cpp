#include "model.h"
#include "json_wire.h"
#include "rotation_runtime.h"
#include "storage_probe.h"
#include "builtin_config_data.h"
#include <LittleFS.h>
#include <memory>
#include <new>

namespace chimera {
namespace {
constexpr char ACTIVE[] = "/config.records";
constexpr char STAGED[] = "/pending.records";
bool mounted = false;
// Boot reads and committed-but-unapplied settings have separate causes.
bool bootReadLocked = false;
bool committedApplyFailed = false;
constexpr char APPLY_FAILED[] = "Settings saved to storage, but runtime apply failed. Outputs disabled. Do not resave; Restart required.";
bool saveLocked(String& error) {
  if (committedApplyFailed) { error = APPLY_FAILED; return true; }
  if (bootReadLocked) { error = "Boot settings read failed; reload or restart before saving"; return true; }
  return false;
}
uint32_t generation = 0, transaction = 0;
uint8_t nextChain = 0;
uint8_t stagedPressCount = 0;
bool rotationStaged = false;
ConfigStorageState storageState = ConfigStorageState::Missing;

bool mount(String& error) {
  if (mounted) return true;
  mounted = LittleFS.begin(false, "/littlefs", 3, "settings");
  if (!mounted) {
    // Only a completely erased NEW filesystem partition may be formatted.
    // A damaged filesystem must not silently erase the user's saved settings.
    if (inspectSettingsEraseState() == SettingsEraseState::Erased)
      mounted = LittleFS.begin(true, "/littlefs", 3, "settings");
  }
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
// Only called for a verified Missing active file, never as error recovery.
// Decode one record at a time; no second Config or full-document allocation.
bool applyBuiltinDefault(String& error) {
  auto& config = activeConfig();
  static_assert(sizeof(builtin::chains) / sizeof(*builtin::chains) == INPUT_COUNT, "Builtin input count");
  JsonDocument doc;
  std::unique_ptr<Chain> chain(new (std::nothrow) Chain);
  if (!chain) { error = "Not enough RAM for builtin chain"; return false; }
  uint8_t pressCount = 0;
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    if (deserializeJson(doc, builtin::chains[i]) || !decodeChain(doc.as<JsonVariantConst>(), i, *chain, error)) return false;
    if (i < 26) {
      if (!(i % 2)) pressCount = chain->count;
      else if (pressCount + chain->count > MAX_KEY_ACTIONS) return false;
    }
    config.chains[i].assign(*chain);
  }
  chain.reset();
  std::unique_ptr<EncoderRotationSettings> rotation(new (std::nothrow) EncoderRotationSettings);
  if (!rotation) { error = "Not enough RAM for builtin rotation"; return false; }
  if (deserializeJson(doc, builtin::rotation) || !decodeRotation(doc.as<JsonVariantConst>(), *rotation, error)) return false;
  config.encoderRotation = *rotation;
  return true;
}
ConfigStorageState inspect(String& error) {
  if (!mount(error)) return ConfigStorageState::IoError;
  const auto presence = probeSettingsFile(ACTIVE);
  if (presence == StoragePresence::Error) { error = "Settings existence check failed"; return ConfigStorageState::IoError; }
  if (presence == StoragePresence::Absent) return ConfigStorageState::Missing;
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
bool configCommittedApplyFailed() { return committedApplyFailed; }
ConfigStorageState configStorageState() { return storageState; }
const char* configStorageStateName() {
  const char* names[] = {"missing", "ready", "unsupportedVersion", "corrupt", "ioError"};
  return names[unsigned(storageState)];
}
bool configOutputsAllowed() { return !committedApplyFailed && (storageState == ConfigStorageState::Ready || storageState == ConfigStorageState::Missing); }
uint32_t beginConfigSave(const String& json, String& error) {
  transaction = 0; rotationStaged = false;
  if (saveLocked(error)) return 0;
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
  if (!encodeNetwork(network, doc)) { error = "Network encode failed"; return 0; }
  JsonDocument header; header["storageVersion"] = 2; header["network"] = doc;
  if (!writeRecord(header, "w", error)) return 0;
  if (++generation == 0) ++generation;
  transaction = generation; error = ""; return transaction;
}
bool stageConfigChain(uint32_t token, uint8_t id, const String& json, String& error) {
  if (saveLocked(error)) { transaction = 0; return false; }
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
  if (!encodeChain(chain, id, doc)) { error = "Chain encode failed"; transaction = 0; return false; }
  if (!writeRecord(doc, "a", error)) { transaction = 0; return false; }
  ++nextChain; error = ""; return true;
}
bool commitConfigSave(uint32_t token, String& error) {
  auto& config = activeConfig();
  if (saveLocked(error)) { transaction = 0; return false; }
  error = "Incomplete or expired configuration transaction";
  if (!transaction || token != transaction || nextChain != INPUT_COUNT || !rotationStaged) return false;
  transaction = 0;
  if (!scan(STAGED, false, error)) return false;
  // LittleFS rename-over-existing is atomic, including across power loss.
  if (!LittleFS.rename(STAGED, ACTIVE)) { error = "LittleFS commit failed"; return false; }
  if (!scan(ACTIVE, true, error)) {
    committedApplyFailed = true;
    defaults(); storageState = ConfigStorageState::IoError;
    rotationRuntime.apply(config.encoderRotation, false);
    error = APPLY_FAILED; return false;
  }
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
  if (storageState == ConfigStorageState::Missing) {
    if (bootReadLocked || committedApplyFailed) { storageState = ConfigStorageState::IoError; message = "Previously unreadable settings now absent; recovery required"; return false; }
    if (!applyBuiltinDefault(message)) {
      defaults(); storageState = ConfigStorageState::IoError; bootReadLocked = true;
      message = "Builtin Default could not be loaded; outputs disabled"; return false;
    }
    rotationRuntime.restart(config.encoderRotation);
    message = "Builtin Default loaded (not saved)"; return true;
  }
  if (storageState != ConfigStorageState::Ready) {
    // Unsupported-version replacement remains an explicit supported operation.
    if (storageState != ConfigStorageState::UnsupportedVersion) bootReadLocked = true;
    return false;
  }
  if (!scan(ACTIVE, false, message) || !scan(ACTIVE, true, message)) { defaults(); storageState = ConfigStorageState::Corrupt; bootReadLocked = true; return false; }
  bootReadLocked = false;
  committedApplyFailed = false;
  rotationRuntime.restart(config.encoderRotation);
  message = "Loaded LittleFS settings"; return true;
}
bool saveWifiConfig(const String& json, String& error) {
  auto& config = activeConfig();
  if (saveLocked(error)) return false;
  if (!configOutputsAllowed()) { error = "Settings unavailable: Wi-Fi save cannot replace existing settings"; return false; }
  // Also invalidate any prior staging when this full-config rewrite fails.
  transaction = 0;
  if (json.length() > 1024) { error = "Wi-Fi request too large"; return false; }
  JsonDocument credentials, doc;
  if (deserializeJson(credentials, json)) { error = "Invalid Wi-Fi JSON"; return false; }
  error = "Settings encode or serialization failed";
  if (!encodeNetwork(config, doc) || !doc["ssid"].set(credentials["ssid"]) ||
      !doc["password"].set(credentials["password"])) return false;
  JsonDocument request; request["schemaVersion"] = 2;
  if (doc.overflowed() || !request["network"].set(doc)) return false;
  String body; if (!checkedSettingsJson(request, body)) return false;
  request.clear();
  credentials.clear(); doc.clear();
  auto token = beginConfigSave(body, error);
  if (!token) return false;
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    if (!encodeChain(config.chains[i], i, doc) || !checkedSettingsJson(doc, body)) {
      error = "Chain encode or serialization failed"; transaction = 0; return false;
    }
    doc.clear();
    if (!stageConfigChain(token, i, body, error)) return false;
  }
  if (!encodeRotation(config.encoderRotation, doc, true) || !checkedSettingsJson(doc, body)) {
    error = "Rotation encode or serialization failed"; transaction = 0; return false;
  }
  doc.clear();
  if (!stageConfigRotation(token, body, error)) return false;
  return commitConfigSave(token, error);
}
bool stageConfigRotation(uint32_t token, const String& json, String& error) {
  if (saveLocked(error)) { transaction = 0; return false; }
  error = "Invalid or expired rotation transaction";
  if (!transaction || token != transaction || nextChain != INPUT_COUNT || rotationStaged) return false;
  if (json.length() > MAX_RECORD_BYTES) { transaction = 0; error = "Rotation request too large"; return false; }
  JsonDocument doc;
  std::unique_ptr<EncoderRotationSettings> rotation(new (std::nothrow) EncoderRotationSettings);
  if (!rotation) { transaction = 0; error = "Not enough RAM for rotation stage"; return false; }
  if (deserializeJson(doc, json) || !decodeRotation(doc.as<JsonVariantConst>(), *rotation, error)) { transaction = 0; return false; }
  if (!encodeRotation(*rotation, doc, true)) { error = "Rotation encode failed"; transaction = 0; return false; }
  if (!writeRecord(doc, "a", error)) { transaction = 0; return false; }
  rotationStaged = true; error = ""; return true;
}
} // namespace chimera
