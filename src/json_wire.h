#pragma once
#include <ArduinoJson.h>

namespace chimera {
// ArduinoJson 7.4 escapes common controls (\n, \t, ...) but emits other C0
// controls literally. Settings allow those in OSC String; JSON must escape them.
template<class Sink> struct SettingsJsonWriter {
  Sink& sink;
  size_t bytes = 0;
  size_t write(uint8_t c) {
    if (c < 32) {
      const char hex[] = "0123456789abcdef";
      const uint8_t escaped[] = {'\\','u','0','0',uint8_t(hex[c >> 4]),uint8_t(hex[c & 15])};
      const auto n = sink.write(escaped, sizeof(escaped)); bytes += n;
      return n == sizeof(escaped) ? 1 : 0;
    }
    const auto n = sink.write(&c, 1); bytes += n; return n;
  }
  size_t write(const uint8_t* data, size_t size) {
    size_t n = 0; for (; n < size; ++n) if (!write(data[n])) break; return n;
  }
};
struct SettingsStringSink {
  String& text;
  size_t write(const uint8_t* data, size_t size) { return text.concat(reinterpret_cast<const char*>(data), size) ? size : 0; }
};
struct SettingsCountSink { size_t write(const uint8_t*, size_t size) { return size; } };
template<class Sink> size_t serializeSettingsJson(const JsonDocument& doc, Sink& sink) {
  SettingsJsonWriter<Sink> writer{sink}; serializeJson(doc, writer); return writer.bytes;
}
inline size_t serializeSettingsJson(const JsonDocument& doc, String& text) {
  SettingsStringSink sink{text}; return serializeSettingsJson(doc, sink);
}
inline size_t measureSettingsJson(const JsonDocument& doc) {
  SettingsCountSink sink; return serializeSettingsJson(doc, sink);
}
}
