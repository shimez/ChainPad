#pragma once
#include "model.h"
#include "backends.h"
#include "chain_loop.h"

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
    // Keep aggregate traffic bounded even when all 32 slots contain OSC Loops.
    static constexpr uint16_t LOOP_CYCLE_BUDGET = 64;
    static constexpr uint8_t LOOP_SLICE_BUDGET = 16;
    static constexpr uint32_t LOOP_OSC_INTERVAL_MS = 4;
    // Call once before HTTP/input dispatch and tick, never from Stop continuation.
    void beginCycle() { loopBudget = LOOP_CYCLE_BUDGET; }
   bool trigger(const InputEvent& event);
   void tick(uint32_t now);
   void cancelAll();
   uint8_t activeCount() const { return running; }
   EngineStats stats;
 private:
     struct Invocation {
       struct Frame { uint16_t remaining; uint8_t start; };
       uint8_t input = 0, next = 0, depth = 0;
       bool pairPending = false, looping = false, oscPending = false;
       uint32_t wakeAt = 0, oscTicket = 0;
       Frame stack[loops::MAX_DEPTH]{};
     } jobs[MAX_RUNNING];
    uint16_t loopBudget = LOOP_CYCLE_BUDGET;
    uint32_t nextOscTicket = 0, lastLoopOscAt = 0;
    bool loopOscClockValid = false;
   uint8_t running = 0;
    enum class AdvanceResult { Complete, Waiting, StopOthers };
     AdvanceResult advance(Invocation& job, uint32_t now, uint8_t& slice);
     bool admitLoopOsc(Invocation& job, uint32_t now);
     void continueAfterStop(Invocation& job, uint32_t now, uint8_t others, uint8_t& slice);
};
extern Engine engine;
}
