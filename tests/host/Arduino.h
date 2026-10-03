#pragma once
// Minimal host adapter, not a firmware replacement. ArduinoJson and the
// production model/engine/backends are compiled unchanged in these tests.
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
class String {
  std::string value;
 public:
  String() = default;
  String(const char* s) : value(s ? s : "") {}
  String(const std::string& s) : value(s) {}
  template<class T, typename = std::enable_if_t<std::is_integral_v<T>>>
  String(T n) : value(std::to_string(n)) {}
  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }
  bool isEmpty() const { return value.empty(); }
  bool concat(const char* s, size_t n) { value.append(s, n); return true; }
  bool concat(const char* s) { value.append(s); return true; }
  friend String operator+(const String& a, const String& b) { return a.value + b.value; }
  friend bool operator==(const String& a, const String& b) { return a.value == b.value; }
  friend bool operator!=(const String& a, const String& b) { return !(a == b); }
};
inline uint32_t hostMillis = 1234;
inline uint32_t millis() { return hostMillis; }
