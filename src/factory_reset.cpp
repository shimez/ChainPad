#include "factory_reset.h"
#include "storage_probe.h"
#include <LittleFS.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_partition.h>
#include <esp_littlefs.h>

namespace chimera {
namespace {
constexpr char PENDING[] = "/factory-reset.pending";
constexpr char TEMP[] = "/factory-reset.tmp";
constexpr char DONE[] = "/factory-reset.done";
constexpr char NS[] = "chainpad_reset";
bool blocked = false, accepted = false;
const char* state = "idle";
struct Store {
  bool fs = false;
  bool erased = false;
  int intent() {
    // Never automatically erase an unreadable NVS here.
    if (nvs_flash_init() != ESP_OK) return -2;
    nvs_handle_t h;
    auto err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return -1;
    if (err != ESP_OK) return -2;
    uint8_t attempt = 0;
    err = nvs_get_u8(h, "attempt", &attempt); nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return -1;
    return err == ESP_OK && attempt <= 3 ? attempt : -2;
  }
  bool saveIntent(int attempt) {
    if (nvs_flash_init() != ESP_OK) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    auto err = nvs_set_u8(h, "attempt", attempt);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h); return err == ESP_OK;
  }
  int marker(const char* path) {
    if (!fs) return erased ? -1 : -2;
    const auto presence = probeSettingsFile(path);
    if (presence == StoragePresence::Absent) return -1;
    if (presence == StoragePresence::Error) return -2;
    auto f = LittleFS.open(path, "r");
    char value[6]{};
    if (!f || f.size() != 5 || f.readBytes(value, 5) != 5) return -2;
    if (memcmp(value, "FR1:", 4) || value[4] < '1' || value[4] > '3') return -2;
    return value[4] - '0';
  }
  int journal() {
    fs = LittleFS.begin(false, "/littlefs", 3, "settings");
    erased = !fs && inspectSettingsEraseState() == SettingsEraseState::Erased;
    return marker(PENDING);
  }
  int completed() { return marker(DONE); }
  bool formatSettings() {
    LittleFS.end(); fs = false;
    // Explicit label: never format an SDK default/other partition.
    if (esp_littlefs_format("settings") != ESP_OK) return false;
    fs = LittleFS.begin(false, "/littlefs", 3, "settings");
    return fs;
  }
  bool saveJournal(int attempt) {
    if (!fs) return false;
    auto f = LittleFS.open(TEMP, "w");
    char value[] = "FR1:0"; value[4] += attempt;
    if (!f) return false;
    const bool ok = f.write(reinterpret_cast<const uint8_t*>(value), 5) == 5;
    f.flush(); f.close();
    if (!ok || !LittleFS.rename(TEMP, PENDING)) return false;
    return journal() == attempt;
  }
  bool eraseNvs() {
    const auto* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (!p) return false;
    auto err = nvs_flash_deinit();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) return false;
    if (esp_partition_erase_range(p, 0, p->size) != ESP_OK) return false;
    // Verify the whole partition before marking completion.
    uint8_t bytes[256];
    for (size_t offset = 0; offset < p->size; offset += sizeof(bytes)) {
      if (esp_partition_read(p, offset, bytes, sizeof(bytes)) != ESP_OK) return false;
      for (auto b : bytes) if (b != 0xff) return false;
    }
    return true;
  }
  bool finish() { return LittleFS.rename(PENDING, DONE); }
};
}
ResetBoot factoryResetBoot() {
  accepted = false;
  Store store;
  const auto result = runFactoryReset(store);
  LittleFS.end();
  blocked = result == ResetBoot::Failed || result == ResetBoot::Restart;
  state = result == ResetBoot::Failed ? "failed" : result == ResetBoot::Complete ? "complete" : "idle";
  return result;
}
bool requestFactoryReset() {
  if (accepted) return true; // Idempotent within the acceptance/reboot window.
  Store store;
  if (!store.saveIntent(0)) return false;
  accepted = blocked = true; state = "accepted";
  return true;
}
bool factoryResetBlocked() { return blocked; }
const char* factoryResetStatus() { return state; }
}
