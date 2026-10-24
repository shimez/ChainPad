#pragma once
#include <cstdint>
struct TestFile {
  explicit operator bool() const { return true; }
  size_t size() const { return INT32_MAX; }
};
struct TestFS {
  bool exists(const char*) { return true; }
  TestFile open(const char*, const char*) { return {}; }
  size_t totalBytes() { return UINT32_MAX; }
  size_t usedBytes() { return UINT32_MAX; }
};
inline TestFS LittleFS;
