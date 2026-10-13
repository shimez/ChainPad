#include "storage_probe.h"
#include <LittleFS.h>
#include <esp_littlefs.h>
#include <esp_partition.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

namespace chimera {
namespace {
constexpr char ROOT[] = "/littlefs";
bool mountedRoot() {
  if (!esp_littlefs_mounted("settings")) return false;
  const char* point = LittleFS.mountpoint();
  if (!point || strcmp(point, ROOT)) return false;
  struct stat info{};
  // An unregistered VFS also reports ENOENT: root failure is always Error.
  return ::stat(ROOT, &info) == 0 && S_ISDIR(info.st_mode);
}
}
StoragePresence probeSettingsFile(const char* path) {
  // All settings/marker files are directly under the fixed mount root.
  if (!path || path[0] != '/' || !path[1] || strchr(path + 1, '/') ||
      !strcmp(path, "/.") || !strcmp(path, "/..") || !mountedRoot()) return StoragePresence::Error;
  char full[80];
  const int n = snprintf(full, sizeof(full), "%s%s", ROOT, path);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(full)) return StoragePresence::Error;
  struct stat info{};
  errno = 0;
  const int result = ::stat(full, &info);
  const int error = errno; // Capture before any other API/logging call.
  if (result == 0) return S_ISREG(info.st_mode) ? StoragePresence::Present : StoragePresence::Error;
  // Recheck root on absence, including removal of the VFS during the probe.
  return error == ENOENT && mountedRoot() ? StoragePresence::Absent : StoragePresence::Error;
}
SettingsEraseState inspectSettingsEraseState() {
  const auto* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "settings");
  if (!p || !p->size) return SettingsEraseState::Error;
  uint8_t bytes[256];
  for (size_t offset = 0; offset < p->size;) {
    const size_t n = p->size - offset < sizeof(bytes) ? p->size - offset : sizeof(bytes);
    if (esp_partition_read(p, offset, bytes, n) != ESP_OK) return SettingsEraseState::Error;
    for (size_t i = 0; i < n; ++i) if (bytes[i] != 0xff) return SettingsEraseState::NotErased;
    offset += n;
  }
  return SettingsEraseState::Erased;
}
}
