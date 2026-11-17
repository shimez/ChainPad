#pragma once
#include "model.h"

namespace chimera {
namespace loops {
constexpr uint16_t MAX_COUNT = 999;
// Derived from the existing Action budget, not a separate nesting policy.
constexpr uint8_t MAX_DEPTH = MAX_ACTIONS / 2;
constexpr uint8_t NO_PARTNER = 0xff;

inline bool marker(const Action& action) {
  return action.protocol == Protocol::LoopStart || action.protocol == Protocol::LoopEnd;
}

enum class ParseResult { NotLoop, Valid, Invalid };

// Phase 1 foundation: the main codec will call this together with scheduler
// integration. Until then its existing unknown-Action rejection stays in place.
// Invalid / NotLoop leave the caller's Action unchanged.
inline ParseResult parse(JsonVariantConst json, Action& out) {
  if (!json.is<JsonObjectConst>()) return ParseResult::NotLoop;
  const auto protocol = json["protocol"].as<JsonString>();
  if (!protocol.c_str()) return ParseResult::NotLoop;
  const bool start = strcmp(protocol.c_str(), "loopStart") == 0;
  const bool end = strcmp(protocol.c_str(), "loopEnd") == 0;
  if (!start && !end) return ParseResult::NotLoop;
  if (protocol.size() != strlen(protocol.c_str()) ||
      !json["delayMs"].is<int64_t>() || json["delayMs"].as<int64_t>() != 0 ||
      json.as<JsonObjectConst>().size() != (start ? 3u : 2u)) return ParseResult::Invalid;
  if (start && (!json["count"].is<int64_t>() ||
                json["count"].as<int64_t>() < 1 ||
                json["count"].as<int64_t>() > MAX_COUNT)) return ParseResult::Invalid;
  out = Action{};
  out.protocol = start ? Protocol::LoopStart : Protocol::LoopEnd;
  out.loopCount = start ? json["count"].as<uint32_t>() : 0;
  return ParseResult::Valid;
}

// Only call for control markers. Shape is canonical, without runtime/UI state.
inline bool encode(const Action& action, JsonObject json) {
  if (json.isNull() || !marker(action) || action.delayMs ||
      (action.protocol == Protocol::LoopStart &&
       (action.loopCount < 1 || action.loopCount > MAX_COUNT))) return false;
  json.clear();
  json["protocol"] = action.protocol == Protocol::LoopStart ? "loopStart" : "loopEnd";
  json["delayMs"] = 0;
  if (action.protocol == Protocol::LoopStart) json["count"] = action.loopCount;
  return json.size() == (action.protocol == Protocol::LoopStart ? 3u : 2u) &&
         json["protocol"].is<const char*>() && json["delayMs"].is<int>() &&
         (action.protocol != Protocol::LoopStart || json["count"].is<uint32_t>());
}

struct Layout {
  uint8_t partner[MAX_ACTIONS];
  uint8_t count = 0, maxDepth = 0;
  Layout() { for (auto& index : partner) index = NO_PARTNER; }
};

// Accepts Chain and ChainView, including the reverse-stride Release view.
// Ordinary Action payloads are the responsibility of the existing Action codec.
// No persistent IDs/jump offsets, heap allocation, recursion or loop expansion.
// The output is empty on failure; publish a layout only after full validation.
template<class ChainLike>
bool analyze(const ChainLike& chain, Layout& out, String& error, uint8_t limit = MAX_ACTIONS) {
  out = Layout{};
  if (limit > MAX_ACTIONS || chain.count > limit) {
    error = "Loop chain exceeds Action limit"; return false;
  }
  Layout next;
  uint8_t stack[MAX_DEPTH]{}, depth = 0;
  for (uint8_t index = 0; index < chain.count; ++index) {
    const auto& action = chain.actions[index];
    if (!marker(action)) continue;
    if (action.delayMs) { error = "Invalid Loop marker at Action " + String(index + 1); return false; }
    if (action.protocol == Protocol::LoopStart) {
      if (action.loopCount < 1 || action.loopCount > MAX_COUNT || depth == MAX_DEPTH) {
        error = "Invalid Loop marker at Action " + String(index + 1); return false;
      }
      stack[depth++] = index;
      if (depth > next.maxDepth) next.maxDepth = depth;
      ++next.count;
    } else {
      if (!depth) { error = "Loop Footer has no Header at Action " + String(index + 1); return false; }
      const uint8_t header = stack[--depth];
      next.partner[header] = index;
      next.partner[index] = header;
    }
  }
  if (depth) { error = "Loop Header has no Footer at Action " + String(stack[depth - 1] + 1); return false; }
  out = next;
  error = "";
  return true;
}
} // namespace loops
} // namespace chimera
