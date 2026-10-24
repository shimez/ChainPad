#include "obs_settings_store.h"
#if CHAINPAD_HAS_OBS
#include "storage_probe.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace chimera { namespace obssettings {
namespace {
constexpr char Active[] = "/littlefs/obs-settings.json";
constexpr char Temp[] = "/littlefs/obs-settings.tmp";
// Use the same mounted LittleFS VFS as Arduino File. POSIX returns errors for
// write/sync/close; Arduino File::flush/close return void and hide these failures.
State readFile(const char* path, ObsSettings& out, Error& error) {
  error = Error::Io;
  int fd = ::open(path, O_RDONLY);
  if (fd < 0) return State::IoError;
  struct stat info{};
  if (::fstat(fd, &info) || info.st_size < 0) { ::close(fd); return State::IoError; }
  if (info.st_size > MaxJsonBytes) {
    const int closed = ::close(fd);
    error = closed ? Error::Io : Error::TooLarge;
    return closed ? State::IoError : State::Corrupt;
  }
  const size_t size = size_t(info.st_size);
  std::unique_ptr<char[]> bytes(new (std::nothrow) char[size + 1]);
  if (!bytes) { ::close(fd); error = Error::NoMemory; return State::IoError; }
  size_t used = 0; bool ok = true;
  while (used < size) {
    const auto n = ::read(fd, bytes.get() + used, size - used);
    if (n <= 0) { ok = false; break; }
    used += size_t(n);
  }
  char extra;
  if (ok && ::read(fd, &extra, 1) != 0) ok = false;
  if (::close(fd)) ok = false;
  if (!ok) return State::IoError;
  bytes[size] = 0;
  const auto state = decodeStored(bytes.get(), size, out, error);
  // Best-effort removal of the raw file copy, not secure-memory encryption.
  volatile char* p = bytes.get(); for (size_t i = 0; i <= size; ++i) p[i] = 0;
  return state;
}
State readActive(ObsSettings& out, Error& error) {
  const auto presence = probeSettingsFile("/obs-settings.json");
  if (presence == StoragePresence::Error) { error = Error::Io; return State::IoError; }
  if (presence == StoragePresence::Absent) { out = ObsSettings{}; error = Error::None; return State::Missing; }
  return readFile(Active, out, error);
}
}
State Store::load(Error& error) {
  ObsSettings candidate;
  state_ = readActive(candidate, error);
  saved_ = settings() ? candidate : ObsSettings{};
  return state_;
}
Error Store::save(const char* json, size_t size, bool replaceInvalid) {
  // Inspect again: a stale RAM snapshot must not silently overwrite a newly
  // damaged file. Failed writes never publish the candidate to RAM.
  ObsSettings baseline; Error error;
  const State disk = readActive(baseline, error);
  const bool healthy = disk == State::Ready || disk == State::Missing;
  if (!healthy) { state_ = disk; saved_ = ObsSettings{}; }
  ObsSettings candidate;
  error = decodeUpdate(json, size, healthy ? &baseline : nullptr, candidate);
  if (error != Error::None) return error;
  // Explicit recovery applies to readable invalid data, not failed I/O. A
  // transient read failure must never authorize replacing an unreadable file.
  if (disk == State::IoError) return Error::Io;
  if (!healthy && !replaceInvalid) return Error::ReplaceRequired;
  // Probe validates that the settings partition is mounted at /littlefs.
  // Even explicit replacement must not bypass a failed presence/root probe.
  if (probeSettingsFile("/obs-settings.json") == StoragePresence::Error) return Error::Io;
  std::unique_ptr<char[]> bytes(new (std::nothrow) char[MaxJsonBytes + 1]);
  if (!bytes) return Error::NoMemory;
  const size_t count = encodeStored(candidate, bytes.get(), MaxJsonBytes + 1);
  if (!count) return Error::NoMemory;
  const int fd = ::open(Temp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return Error::Io;
  bool ok = ::write(fd, bytes.get(), count) == static_cast<decltype(::write(fd, bytes.get(), count))>(count);
  if (ok && ::fsync(fd)) ok = false;
  if (::close(fd)) ok = false;
  volatile char* p = bytes.get(); for (size_t i = 0; i <= count; ++i) p[i] = 0;
  bytes.reset();
  if (!ok) return Error::Io;
  ObsSettings verified;
  if (readFile(Temp, verified, error) != State::Ready || !equal(candidate, verified)) return Error::Verify;
  // Never remove Active first. VFS rename delegates to LittleFS replacement.
  if (::rename(Temp, Active)) return Error::Rename;
  saved_ = candidate; state_ = State::Ready; return Error::None;
}
} }
#endif
