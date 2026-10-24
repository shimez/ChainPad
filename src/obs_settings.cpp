#include "obs_settings.h"
#if CHAINPAD_HAS_OBS
#include <IPAddress.h>
#include <cstring>
#include <cstdlib>

namespace chimera { namespace obssettings {
namespace {
// Local JSON budget, including allocation headers. No full Config allocation.
class Budget : public ArduinoJson::Allocator {
  struct alignas(max_align_t) Header { size_t size; };
  size_t used = 0;
  static constexpr size_t Limit = 8192;
public:
  void* allocate(size_t n) override {
    if (n > Limit - used || sizeof(Header) > Limit - used - n) return nullptr;
    auto p = static_cast<Header*>(malloc(sizeof(Header) + n));
    if (!p) return nullptr;
    p->size = n; used += sizeof(Header) + n; return p + 1;
  }
  void deallocate(void* p) override {
    if (!p) return;
    auto h = static_cast<Header*>(p) - 1;
    used -= sizeof(Header) + h->size; free(h);
  }
  void* reallocate(void* p, size_t n) override {
    if (!p) return allocate(n);
    auto h = static_cast<Header*>(p) - 1;
    if (n > Limit - (used - h->size)) return nullptr;
    const auto old = h->size;
    auto next = static_cast<Header*>(realloc(h, sizeof(Header) + n));
    if (!next) return nullptr;
    next->size = n; used = used - old + n; return next + 1;
  }
};
// ArduinoJson stops at the closing brace. Require complete input consumption.
struct Reader {
  const char* bytes; size_t size, pos = 0;
  int read() { return pos < size ? uint8_t(bytes[pos++]) : -1; }
  size_t readBytes(char* out, size_t n) {
    size_t i = 0; for (; i < n && pos < size; ++i) out[i] = bytes[pos++]; return i;
  }
};
Error parse(const char* json, size_t size, JsonDocument& doc) {
  if (!json || !size) return Error::InvalidInput;
  if (size > MaxJsonBytes) return Error::TooLarge;
  if (memchr(json, 0, size)) return Error::InvalidInput;
  Reader reader{json, size};
  const auto e = deserializeJson(doc, reader, DeserializationOption::NestingLimit(2));
  if (e == DeserializationError::NoMemory) return Error::NoMemory;
  if (e || !doc.is<JsonObject>()) return Error::InvalidInput;
  while (reader.pos < size) {
    char c = json[reader.pos++];
    if (c != ' ' && c != '\r' && c != '\n' && c != '\t') return Error::InvalidInput;
  }
  return Error::None;
}
bool text(JsonVariantConst v, char* out, size_t max) {
  if (!v.is<const char*>()) return false;
  auto s = v.as<JsonString>();
  if (s.size() > max || memchr(s.c_str(), 0, s.size()) || !obs::validText(s.c_str(), max, true)) return false;
  memcpy(out, s.c_str(), s.size() + 1); return true;
}
bool valid(const ObsSettings& s) {
  if (!obs::validText(s.ip, 15, !s.enabled) || !s.port || !obs::validText(s.password, 128, true)) return false;
  if (!s.ip[0]) return true;
  // fromString also accepts IPv6. Restrict the alphabet before using the same
  // Arduino parser as the Worker; no DNS, signs, whitespace or URL syntax.
  for (const char* p = s.ip; *p; ++p) if ((*p < '0' || *p > '9') && *p != '.') return false;
  IPAddress ip; return ip.fromString(s.ip);
}
Error fields(JsonObjectConst obj, const ObsSettings* previous, bool stored, ObsSettings& out) {
  for (JsonPairConst p : obj) {
    const char* key = p.key().c_str();
    if (strcmp(key, "enabled") && strcmp(key, "ip") && strcmp(key, "port") && strcmp(key, "password") &&
        (!stored || strcmp(key, "storageVersion"))) return Error::InvalidInput;
  }
  ObsSettings candidate;
  if (!obj["enabled"].is<bool>() || !obj["port"].is<unsigned>() ||
      !obj["port"].as<unsigned>() || obj["port"].as<unsigned>() > 65535 ||
      !text(obj["ip"], candidate.ip, 15)) return Error::InvalidInput;
  candidate.enabled = obj["enabled"]; candidate.port = obj["port"];
  if (obj["password"].isUnbound()) {
    if (stored) return Error::InvalidInput;
    if (!previous) return Error::PasswordRequired;
    memcpy(candidate.password, previous->password, sizeof(candidate.password));
  } else if (!text(obj["password"], candidate.password, 128)) return Error::InvalidInput;
  if (!valid(candidate)) return Error::InvalidInput;
  out = candidate; return Error::None;
}
size_t encode(const ObsSettings& s, char* out, size_t capacity, bool stored) {
  if (!out || !capacity) return 0;
  out[0] = 0;
  if (!valid(s)) return 0;
  Budget budget; JsonDocument doc(&budget);
  if (stored) doc["storageVersion"] = 1;
  doc["enabled"] = s.enabled; doc["ip"] = s.ip; doc["port"] = s.port;
  if (stored) doc["password"] = s.password;
  else doc["passwordConfigured"] = s.password[0] != 0;
  const size_t n = measureJson(doc);
  if (doc.overflowed() || n > MaxJsonBytes || n >= capacity) return 0;
  const size_t written = serializeJson(doc, out, capacity);
  if (written != n) { out[0] = 0; return 0; }
  return written;
}
}
const char* stateName(State s) {
  switch (s) {
    case State::Missing: return "missing"; case State::Ready: return "ready";
    case State::Corrupt: return "corrupt"; case State::UnsupportedVersion: return "unsupportedVersion";
    default: return "ioError";
  }
}
const char* errorName(Error e) {
  switch (e) {
    case Error::None: return "none"; case Error::InvalidInput: return "invalidInput";
    case Error::TooLarge: return "tooLarge"; case Error::NoMemory: return "noMemory";
    case Error::PasswordRequired: return "passwordRequired"; case Error::ReplaceRequired: return "replaceRequired";
    case Error::Verify: return "verifyFailed"; case Error::Rename: return "renameFailed";
    default: return "ioError";
  }
}
bool equal(const ObsSettings& a, const ObsSettings& b) {
  return a.enabled == b.enabled && a.port == b.port && !strcmp(a.ip, b.ip) && !strcmp(a.password, b.password);
}
Error decodeUpdate(const char* json, size_t size, const ObsSettings* previous, ObsSettings& out) {
  Budget budget; JsonDocument doc(&budget);
  const auto e = parse(json, size, doc);
  return e == Error::None ? fields(doc.as<JsonObjectConst>(), previous, false, out) : e;
}
State decodeStored(const char* json, size_t size, ObsSettings& out, Error& error) {
  Budget budget; JsonDocument doc(&budget);
  error = parse(json, size, doc);
  if (error != Error::None) return error == Error::NoMemory ? State::IoError : State::Corrupt;
  if (!doc["storageVersion"].is<unsigned>()) { error = Error::InvalidInput; return State::Corrupt; }
  if (doc["storageVersion"].as<unsigned>() != 1) return State::UnsupportedVersion;
  error = fields(doc.as<JsonObjectConst>(), nullptr, true, out);
  return error == Error::None ? State::Ready : State::Corrupt;
}
size_t encodeStored(const ObsSettings& s, char* out, size_t capacity) { return encode(s, out, capacity, true); }
size_t encodePublic(const ObsSettings& s, char* out, size_t capacity) { return encode(s, out, capacity, false); }
} }
#endif
