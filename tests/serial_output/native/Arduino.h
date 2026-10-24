#pragma once
#include "../../host/Arduino.h"
#include <cstdarg>
#include <cstdio>
inline uint32_t serialClockUs = 0;
inline uint32_t micros() { return serialClockUs; }
class Print {
  int error_ = 0;
protected:
  void setWriteError(int value = 1) { error_ = value; }
public:
  virtual ~Print() = default;
  int getWriteError() const { return error_; }
  void clearWriteError() { error_ = 0; }
  virtual size_t write(uint8_t value) { output.push_back(char(value)); return 1; }
  virtual size_t write(const uint8_t* data, size_t size) { output.append(reinterpret_cast<const char*>(data), size); return size; }
  size_t print(const char* text) { return write(reinterpret_cast<const uint8_t*>(text), strlen(text)); }
  size_t print(const String& text) { return print(text.c_str()); }
  size_t println() { return print("\r\n"); }
  size_t println(const char* text) { return print(text) + println(); }
  size_t println(const String& text) { return print(text) + println(); }
  void printf(const char* fmt, ...) {
    char text[4096]; va_list args; va_start(args, fmt); int n = vsnprintf(text, sizeof(text), fmt, args); va_end(args);
    if (n >= 0 && n < int(sizeof(text))) write(reinterpret_cast<const uint8_t*>(text), size_t(n));
  }
  std::string output;
};
