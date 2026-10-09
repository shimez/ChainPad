#pragma once
#include "model.h"
#include "backends.h"

namespace chimera {
// Logical event IDs are independent of matrix rows, columns and GPIOs.
// 0..23: KEY1..12 press/release, 24/25: push, 26/27: CW/CCW.
struct InputEvent { uint8_t input; uint32_t timeMs; };
struct EngineStats {
  uint32_t events = 0, accepted = 0, skipped = 0;
  uint32_t rejectedChains = 0, cancelledChains = 0;
  uint8_t lastInput = 0, lastAction = 0;
  SendResult lastResult = SendResult::Accepted;
};
class Engine {
  public:
   static constexpr uint8_t MAX_RUNNING = 32;
   bool trigger(const InputEvent& event);
   void tick(uint32_t now);
   void cancelAll();
   uint8_t activeCount() const { return running; }
   EngineStats stats;
 private:
    struct Invocation { uint8_t input = 0, next = 0; bool pairPending = false; uint32_t wakeAt = 0; } jobs[MAX_RUNNING];
   uint8_t running = 0;
   bool advance(Invocation& job, uint32_t now);
};
extern Engine engine;
}
