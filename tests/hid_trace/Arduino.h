#pragma once
#include "../host/Arduino.h"
#include <cstdarg>
#include <cstdio>
class Print {
 public:
  std::string text;
  void printf(const char* format, ...) {
    char buffer[512]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); text += buffer;
  }
  void println(const char* value) { text += value; text += '\n'; }
};
