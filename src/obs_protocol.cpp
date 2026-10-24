#include "capabilities.h"
#if CHAINPAD_HAS_OBS
#include "obs_protocol.h"
#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace chimera { namespace obs {
namespace {
// Same bounded allocator as the verified PoC; includes allocation headers.
class Budget : public ArduinoJson::Allocator {
  struct alignas(max_align_t) Header { size_t size; };
  size_t used = 0;
  static constexpr size_t LIMIT = 8192;
public:
  void* allocate(size_t n) override {
    if (n > LIMIT - used || sizeof(Header) > LIMIT - used - n) return nullptr;
    auto h = static_cast<Header*>(malloc(sizeof(Header) + n));
    if (!h) return nullptr;
    h->size = n; used += n + sizeof(Header); return h + 1;
  }
  void deallocate(void* p) override {
    if (!p) return;
    auto h = static_cast<Header*>(p) - 1;
    used -= h->size + sizeof(Header); free(h);
  }
  void* reallocate(void* p, size_t n) override {
    if (!p) return allocate(n);
    auto h = static_cast<Header*>(p) - 1;
    if (n > LIMIT - (used - h->size)) return nullptr;
    const size_t old = h->size;
    auto next = static_cast<Header*>(realloc(h, sizeof(Header) + n));
    if (!next) return nullptr;
    next->size = n; used = used - old + n; return next + 1;
  }
};
bool encode(JsonDocument& doc, char* out, size_t capacity) {
  if (doc.overflowed() || measureJson(doc) >= capacity) return false;
  return serializeJson(doc, out, capacity) > 0;
}
}
bool validText(const char* s, size_t maxBytes, bool allowEmpty) {
  if (!s || (!allowEmpty && !*s)) return false;
  // Bounded scan: the caller supplies a terminated string or maxBytes+1 bytes.
  size_t length = 0;
  while (length <= maxBytes && s[length]) ++length;
  if (length > maxBytes) return false;
  for (size_t i = 0; i < length;) {
    uint8_t c = uint8_t(s[i++]);
    if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; continue; }
    unsigned count; uint32_t value, minimum;
    if (c >= 0xc2 && c <= 0xdf) { count = 1; value = c & 31; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { count = 2; value = c & 15; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { count = 3; value = c & 7; minimum = 0x10000; }
    else return false;
    if (count > length - i) return false;
    while (count--) { c = uint8_t(s[i++]); if ((c & 0xc0) != 0x80) return false; value = (value << 6) | (c & 63); }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}
bool terminalClose(uint16_t code) {
  return code >= 4000 || code == 1002 || code == 1003 || code == 1007 || code == 1008 || code == 1009;
}
void Protocol::connected() { state = State::Authenticating; pending = 0; wire[0] = 0; }
Received Protocol::receive(const char* data, size_t size, const char* password, Authenticate auth) {
  if (!data || !size || size > MESSAGE_LIMIT || memchr(data, 0, size)) return Received::Invalid;
  Budget budget; JsonDocument doc(&budget);
  if (deserializeJson(doc, data, size, DeserializationOption::NestingLimit(6)) || !doc["op"].is<unsigned>() || !doc["d"].is<JsonObject>()) return Received::Invalid;
  auto d = doc["d"].as<JsonObject>(); const unsigned op = doc["op"];
  if (op == 0) {
    if (state != State::Authenticating || wire[0] || !d["rpcVersion"].is<unsigned>() || d["rpcVersion"].as<unsigned>() < 1) return Received::Invalid;
    char authentication[45]{};
    const bool required = !d["authentication"].isNull();
    if (required) {
      auto a = d["authentication"];
      const char* salt = a["salt"], *challenge = a["challenge"];
      if (!validText(salt, 128) || !validText(challenge, 128) ||
          a["salt"].as<JsonString>().size() != strlen(salt) || a["challenge"].as<JsonString>().size() != strlen(challenge) ||
          !auth || !auth(password, salt, challenge, authentication)) return Received::Invalid;
    }
    doc.clear(); doc["op"] = 1;
    doc["d"]["rpcVersion"] = 1; doc["d"]["eventSubscriptions"] = 0;
    if (required) doc["d"]["authentication"] = authentication;
    if (!encode(doc, wire, sizeof(wire))) return Received::Invalid;
    return Received::Identify;
  }
  if (op == 2) {
    if (state != State::Authenticating || !wire[0] || !d["negotiatedRpcVersion"].is<unsigned>() || d["negotiatedRpcVersion"] != 1) return Received::Invalid;
    state = State::Ready; wire[0] = 0; return Received::Ready;
  }
  if (op != 7 || state != State::Ready || !d["requestId"].is<const char*>()) return Received::Invalid;
  char expected[32]; snprintf(expected, sizeof(expected), "chainpad-%lu", static_cast<unsigned long>(pending));
  if (!pending || strcmp(d["requestId"], expected) || d["requestId"].as<JsonString>().size() != strlen(expected)) return Received::Ignored;
  if (d["requestType"] != "SetCurrentProgramScene" || !d["requestStatus"]["result"].is<bool>() || !d["requestStatus"]["code"].is<int>()) return Received::Invalid;
  success = d["requestStatus"]["result"]; code = d["requestStatus"]["code"];
  if (success && code != 100) return Received::Invalid;
  // Server-controlled comments never cross into diagnostics/API/logs.
  return Received::Result;
}
bool Protocol::request(const char* scene, uint32_t id, uint32_t now) {
  if (state != State::Ready || pending || !id || !validText(scene, SCENE_LIMIT)) return false;
  Budget budget; JsonDocument doc(&budget);
  char name[32]; snprintf(name, sizeof(name), "chainpad-%lu", static_cast<unsigned long>(id));
  doc["op"] = 6; auto d = doc["d"].to<JsonObject>();
  d["requestType"] = "SetCurrentProgramScene"; d["requestId"] = name;
  d["requestData"]["sceneName"] = scene;
  if (!encode(doc, wire, sizeof(wire))) return false;
  pending = id; sentAt = now; return true;
}
bool Protocol::expire(uint32_t now) const { return pending && uint32_t(now - sentAt) >= RESPONSE_TIMEOUT_MS; }
void Assembly::reset() { active = false; length = 0; bytes[0] = 0; }
bool Assembly::append(const uint8_t* data, size_t size, bool first, uint32_t now) {
  if (first) { if (active) return false; reset(); active = true; started = now; }
  if (!active || size > MESSAGE_LIMIT - length || (size && !data)) return false;
  if (size) memcpy(bytes + length, data, size);
  length += size; bytes[length] = 0; return true;
}
bool Assembly::expired(uint32_t now) const { return active && uint32_t(now - started) >= HANDSHAKE_TIMEOUT_MS; }
} }
#endif
