#pragma once
#include "model.h"

namespace chimera::calls {
// Runtime admission is separate from the wire format and graph validity.
constexpr bool ENGINE_SUPPORTS_CALL = false;
inline bool executable(const Chain& chain, bool supportsCall = ENGINE_SUPPORTS_CALL) {
  for (uint8_t i = 0; i < chain.count; ++i)
    if (chain.actions[i].protocol == Protocol::CallChain && !supportsCall) return false;
  return true;
}

inline bool decode(JsonVariantConst j, Action& out) {
  const auto protocol = j["protocol"].as<JsonString>();
  if (!protocol.c_str() || protocol.size() != sizeof("callChain") - 1 ||
      strcmp(protocol.c_str(), "callChain") || j.as<JsonObjectConst>().size() != 3 ||
      !j["delayMs"].is<int64_t>() || j["delayMs"].as<int64_t>() != 0 ||
      !j["targetInput"].is<uint8_t>() || j["targetInput"].as<uint8_t>() >= INPUT_COUNT) return false;
  out = Action{};
  out.protocol = Protocol::CallChain; out.targetInput = j["targetInput"].as<uint8_t>();
  return true;
}
inline bool encode(const Action& a, JsonObject j) {
  if (j.isNull() || a.protocol != Protocol::CallChain || a.delayMs || a.targetInput >= INPUT_COUNT) return false;
  j.clear(); j["protocol"] = "callChain"; j["delayMs"] = 0; j["targetInput"] = a.targetInput;
  return j.size() == 3 && j["targetInput"].is<uint8_t>();
}

// Stream one locally validated Chain/ChainView at a time. Edges keep logical
// Action indices (including Release's reverse-stride view). No Action copies.
struct Edge { uint8_t input, index, target; };
struct Cycle { uint8_t count = 0; Edge edges[INPUT_COUNT]; };
enum class Error { None, InvalidInput, Duplicate, Missing, Slots, InvalidCall, Cycle };
class Graph {
  static_assert(INPUT_COUNT < 32, "seen mask");
  uint32_t seen = 0;
  bool containsCall = false;
  uint8_t counts[INPUT_COUNT]{};
  uint8_t targets[INPUT_COUNT][MAX_ACTIONS]{}; // target + 1, or zero for ordinary Action
public:
  template<class T> Error add(unsigned id, const T& chain) {
    if (id >= INPUT_COUNT) return Error::InvalidInput;
    const uint32_t bit = uint32_t(1) << id;
    if (seen & bit) return Error::Duplicate;
    if (chain.count > eventActionLimit(id) ||
        (id < 26 && counts[id ^ 1] + chain.count > MAX_KEY_ACTIONS)) return Error::Slots;
    for (uint8_t i = 0; i < chain.count; ++i) {
      const auto& a = chain.actions[i];
      if (a.protocol == Protocol::CallChain && (a.targetInput >= INPUT_COUNT || a.delayMs)) return Error::InvalidCall;
    }
    counts[id] = chain.count; seen |= bit;
    for (uint8_t i = 0; i < chain.count; ++i)
      if (chain.actions[i].protocol == Protocol::CallChain) {
        targets[id][i] = chain.actions[i].targetInput + 1;
        containsCall = true;
      }
    return Error::None;
  }
  Error validate(Cycle& cycle) const {
    cycle.count = 0;
    if (seen != (uint32_t(1) << INPUT_COUNT) - 1) return Error::Missing;
    if (!containsCall) return Error::None;
    uint8_t color[INPUT_COUNT]{}; // 0 unvisited, 1 on path, 2 complete
    struct Cursor { uint8_t id, next; } path[INPUT_COUNT];
    for (uint8_t root = 0; root < INPUT_COUNT; ++root) {
      if (color[root]) continue;
      uint8_t depth = 1; path[0] = {root, 0}; color[root] = 1;
      while (depth) {
        auto& cursor = path[depth - 1];
        if (cursor.next == counts[cursor.id]) { color[cursor.id] = 2; --depth; continue; }
        const uint8_t ref = targets[cursor.id][cursor.next++];
        if (!ref || color[ref - 1] == 2) continue;
        const uint8_t child = ref - 1;
        if (!color[child]) { path[depth++] = {child, 0}; color[child] = 1; continue; }
        uint8_t first = 0;
        while (path[first].id != child) ++first;
        for (uint8_t k = first; k < depth; ++k) {
          const uint8_t id = path[k].id, index = path[k].next - 1;
          cycle.edges[cycle.count++] = {id, index, uint8_t(targets[id][index] - 1)};
        }
        return Error::Cycle;
      }
    }
    return Error::None;
  }
  enum class Admission { Valid, Invalid, Unsupported };
  Admission finish(bool engineSupportsCall, String& error) const {
    Cycle cycle;
    switch (validate(cycle)) {
      case Error::None: break;
      case Error::Cycle:
        error = "Call cycle: ";
        for (uint8_t i = 0; i < cycle.count; ++i) {
          const auto& e = cycle.edges[i];
          error = error + inputName(e.input) + "#" + String(e.index + 1) + " -> ";
        }
        error = error + inputName(cycle.edges[0].input);
        return Admission::Invalid;
      default:
        error = "Incomplete Call graph"; return Admission::Invalid;
    }
    if (containsCall && !engineSupportsCall) {
      error = "Call Chain is unsupported by this firmware"; return Admission::Unsupported;
    }
    error = ""; return Admission::Valid;
  }
};
}
