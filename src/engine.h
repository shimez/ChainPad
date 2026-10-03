#pragma once
#include "model.h"
#include "backends.h"

namespace chimera {
// Logical event IDs are independent of matrix rows, columns and GPIOs.
// 0..23: KEY1..12 press/release, 24/25: push, 26/27: CW/CCW.
struct InputEvent { uint8_t input; uint32_t timeMs; };
struct EngineStats {
  uint32_t events = 0, accepted = 0, skipped = 0;
  uint8_t lastInput = 0, lastAction = 0;
  SendResult lastResult = SendResult::Accepted;
};
class Engine {
 public:
  bool trigger(const InputEvent& event);
  EngineStats stats;
};
extern Engine engine;
}
