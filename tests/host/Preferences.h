#pragma once
#include "Arduino.h"
namespace fake { inline String stored; inline bool writeFailure = false; }
class Preferences {
 public:
  bool begin(const char*, bool, const char*) { return true; }
  void end() {}
  size_t getBytesLength(const char*) { return fake::stored.length(); }
  size_t getBytes(const char*, void* output, size_t size) { memcpy(output, fake::stored.c_str(), size); return size; }
  size_t putBytes(const char*, const void* data, size_t size) {
    if (fake::writeFailure) return 0;
    fake::stored = String(std::string(static_cast<const char*>(data), size)); return size;
  }
};
