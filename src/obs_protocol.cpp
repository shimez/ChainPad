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
  const size_t limit;
public:
  explicit Budget(size_t bytes = 8192) : limit(bytes) {}
  void* allocate(size_t n) override {
    if (n > limit - used || sizeof(Header) > limit - used - n) return nullptr;
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
    if (n > limit - (used - h->size)) return nullptr;
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
bool terminalClose(uint16_t code) {
  return code >= 4000 || code == 1002 || code == 1003 || code == 1007 || code == 1008 || code == 1009;
}
void Protocol::connected() { state = State::Authenticating; pending = 0; wire[0] = 0; }
Received Protocol::receive(const char* data, size_t size, const char* password, Authenticate auth) {
  if (!data || !size || size > MESSAGE_LIMIT || memchr(data, 0, size)) return Received::Invalid;
  const bool discovery = pending && obs::discovery(operation);
  if (!discovery && size > CONTROL_MESSAGE_LIMIT) {
    // A cancelled auxiliary request may still reply while an Action is pending.
    // Inspect only its envelope, without allocating the returned names.
    Budget budget; JsonDocument filter(&budget), envelope(&budget);
    filter["op"] = true; filter["d"]["requestId"] = true;
    if (deserializeJson(envelope, data, size, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(6))) return Received::Invalid;
    char expected[32]; snprintf(expected, sizeof(expected), "chainpad-%lu", static_cast<unsigned long>(pending));
    const char* id = envelope["d"]["requestId"];
    if (envelope["op"] == 7 && id && (!pending || strcmp(id, expected))) return Received::Ignored;
    return Received::Invalid;
  }
  Budget budget(discovery ? 32768 : 8192); JsonDocument doc(&budget);
  const auto parseError = deserializeJson(doc, data, size, DeserializationOption::NestingLimit(6));
  if (discovery && sceneMemoryProbe) sceneMemoryProbe();
  if (discovery && parseError == DeserializationError::NoMemory) {
    // Auxiliary discovery may exhaust its budget or the heap. Fail only this
    // request; do not publish partial names or tear down a healthy connection.
    success = false; code = -10; return Received::Result;
  }
  if (parseError || !doc["op"].is<unsigned>() || !doc["d"].is<JsonObject>()) return Received::Invalid;
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
  if (d["requestType"] != requestType(operation) || !d["requestStatus"]["result"].is<bool>() || !d["requestStatus"]["code"].is<int>()) return Received::Invalid;
  success = d["requestStatus"]["result"]; code = d["requestStatus"]["code"];
  if (success && code != 100) return Received::Invalid;
  if (success && discovery) {
    // A bounded but incomplete list is a failure, never an authoritative cache.
    const bool inputs = operation == Operation::GetInputList;
    auto list = d["responseData"][inputs ? "inputs" : "scenes"];
    if (!scenes || !list.is<JsonArray>() || (!inputs && list.size() > SCENE_COUNT_LIMIT)) {
      success = false; code = -10; return Received::Result;
    }
    scenes->count = scenes->used = 0;
    for (auto entry : list.as<JsonArray>()) {
      if (inputs) {
        if (!entry["inputKindCaps"].is<uint32_t>()) {
          scenes->count = scenes->used = 0; success = false; code = -11; return Received::Result;
        }
        if (!(entry["inputKindCaps"].as<uint32_t>() & 2u)) continue;
      }
      auto name = entry[inputs ? "inputName" : "sceneName"].as<JsonString>();
      if (!name.c_str() || name.size() != strlen(name.c_str()) || !validText(name.c_str(), TARGET_LIMIT) ||
          scenes->count >= SCENE_COUNT_LIMIT || name.size() + 1 > SCENE_BYTES_LIMIT - scenes->used) {
        scenes->count = scenes->used = 0; success = false; code = -10; return Received::Result;
      }
      scenes->offsets[scenes->count++] = scenes->used;
      memcpy(scenes->names + scenes->used, name.c_str(), name.size() + 1);
      scenes->used += name.size() + 1;
    }
  }
  // Server-controlled comments never cross into diagnostics/API/logs.
  return Received::Result;
}
bool Protocol::request(const char* scene, uint32_t id, uint32_t now) {
  if (!validText(scene, SCENE_LIMIT)) return false;
  Command command; memcpy(command.target, scene, strlen(scene) + 1);
  return request(command, id, now);
}
bool Protocol::request(const Command& command, uint32_t id, uint32_t now) {
  if (state != State::Ready || pending || !id || !validCommand(command)) return false;
  Budget budget; JsonDocument doc(&budget);
  char name[32]; snprintf(name, sizeof(name), "chainpad-%lu", static_cast<unsigned long>(id));
  doc["op"] = 6; auto d = doc["d"].to<JsonObject>();
  d["requestType"] = requestType(command.operation); d["requestId"] = name;
  if (named(command.operation)) d["requestData"][command.operation == Operation::SetCurrentProgramScene ? "sceneName" : "inputName"] = command.target;
  if (command.operation == Operation::SetInputMute) d["requestData"]["inputMuted"] = command.muted;
  if (!encode(doc, wire, sizeof(wire))) return false;
  operation = command.operation; pending = id; sentAt = now; return true;
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
