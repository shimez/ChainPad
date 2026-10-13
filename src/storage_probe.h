#pragma once

namespace chimera {
enum class StoragePresence { Present, Absent, Error };
enum class SettingsEraseState { Erased, NotErased, Error };
// Read-only checks. Never infer absence from Arduino File/open failure.
StoragePresence probeSettingsFile(const char* path);
SettingsEraseState inspectSettingsEraseState();
}
