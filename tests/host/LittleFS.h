#pragma once
#include "Arduino.h"
#include <map>
#include <memory>
#include <algorithm>
namespace fake {
inline bool writeFailure = false, renameFailure = false;
inline int writesUntilFailure = -1;
inline std::map<std::string, std::shared_ptr<std::string>> files;
}
class File {
  std::shared_ptr<std::string> data;
  size_t position = 0;
  bool writable = false;
public:
  File() = default;
  File(std::shared_ptr<std::string> value, bool write, bool append) : data(value), writable(write) {
    if (append) position = data->size();
  }
  explicit operator bool() const { return bool(data); }
  int read() { return data && position < data->size() ? uint8_t((*data)[position++]) : -1; }
  size_t write(const uint8_t* bytes, size_t n) {
    if (!data || !writable || fake::writeFailure || fake::writesUntilFailure == 0) return 0;
    if (fake::writesUntilFailure > 0) --fake::writesUntilFailure;
    data->append(reinterpret_cast<const char*>(bytes), n); position += n; return n;
  }
  size_t write(uint8_t b) { return write(&b, 1); }
  void flush() {}
  void close() { data.reset(); }
};
struct FakeLittleFS {
  bool begin(bool, const char*, int, const char*) { return true; }
  bool exists(const char* path) { return fake::files.count(path); }
  File open(const char* path, const char* mode) {
    if (*mode == 'w') fake::files[path] = std::make_shared<std::string>();
    if (!exists(path)) return {};
    return File(fake::files[path], *mode != 'r', *mode == 'a');
  }
  bool rename(const char* from, const char* to) {
    if (fake::renameFailure || !exists(from)) return false;
    fake::files[to] = fake::files[from]; fake::files.erase(from); return true;
  }
};
inline FakeLittleFS LittleFS;
