#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <atomic>
#include <cstdarg>
#include <cstdio>
class Print {
public:
  std::string output;
  void println(const char* s) { output += s; output += '\n'; }
  void printf(const char* format, ...) {
    char buffer[2048];va_list args;va_start(args,format);vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);output+=buffer;
  }
};
class String {
  std::string data;
public:
  String(const char* s = "") : data(s ? s : "") {}
  String(const std::string& s) : data(s) {}
  const char* c_str() const { return data.c_str(); }
  size_t length() const { return data.size(); }
  void trim() {
    auto end = data.find_last_not_of(" \r\n\t");
    if (end == std::string::npos) data.clear();
    else data = data.substr(0, end + 1).substr(data.find_first_not_of(" \r\n\t"));
  }
  friend String operator+(const String& a, const String& b) { return a.data + b.data; }
};
inline std::atomic<uint32_t> hostMillis{100};
inline uint32_t millis() { return hostMillis; }
using ssize_t = ptrdiff_t;
#define bit(n) (1UL << (n))
#define ESP_IDF_VERSION_MAJOR 5
#define ESP_ARDUINO_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define ESP_ARDUINO_VERSION 30311
inline void yield() { ++hostMillis; }
inline void delay(unsigned n) { hostMillis += n; }
inline uint32_t micros() { return hostMillis * 1000; }
inline long random(long n) { return n > 0 ? 42 % n : 0; }
struct FakeESP { size_t getFreeHeap() const { return 100000; } };
inline FakeESP ESP;
