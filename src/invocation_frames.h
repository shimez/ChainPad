#pragma once
#include "chain_loop.h"

namespace chimera {
// Suspended parents keep their Loop frames in place. Along an acyclic call
// path every live Loop owns a distinct header/footer pair in the 224 slots.
struct InvocationFrames {
  static constexpr uint8_t LOOP_CAPACITY = MAX_TOTAL_ACTIONS / 2;
  static constexpr uint8_t CALL_CAPACITY = INPUT_COUNT - 1;
  static_assert(MAX_ACTIONS <= 31 && INPUT_COUNT <= 32 && loops::MAX_DEPTH <= 15);
  static_assert(loops::MAX_COUNT < 1024);
  uint16_t loops[LOOP_CAPACITY], calls[CALL_CAPACITY];
  uint8_t top = 0, local = 0, depth = 0;

  void reset() { top = local = depth = 0; }
  bool enterLoop(uint32_t count, uint8_t start) {
    if (!count || count > loops::MAX_COUNT || start > MAX_ACTIONS ||
        local >= loops::MAX_DEPTH || top >= LOOP_CAPACITY) return false;
    loops[top++] = uint16_t((start << 10) | count); ++local;
    return true;
  }
  bool endLoop(uint8_t& next) {
    if (!local || !top) return false; // Never pop a suspended parent's frame.
    auto& frame = loops[top - 1];
    if ((frame & 1023) == 1) { --top; --local; }
    else { --frame; next = uint8_t(frame >> 10); }
    return true;
  }
  bool suspend(uint8_t parent, uint8_t next) {
    if (depth >= CALL_CAPACITY || parent >= INPUT_COUNT || next > MAX_ACTIONS) return false;
    calls[depth++] = uint16_t(parent | (next << 5) | (local << 10));
    local = 0;
    return true;
  }
  bool restore(uint8_t& parent, uint8_t& next) {
    if (!depth || local) return false;
    const auto frame = calls[--depth];
    parent = frame & 31; next = (frame >> 5) & 31; local = frame >> 10;
    return true;
  }
};
}
