#pragma once
#include "model.h"
#include "backends.h"
#include "chain_loop.h"
#include "invocation_frames.h"

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
    Engine();
    // Keep aggregate traffic bounded even when all 32 slots contain OSC Loops.
    static constexpr uint16_t LOOP_CYCLE_BUDGET = 64;
    static constexpr uint8_t LOOP_SLICE_BUDGET = 16;
    static constexpr uint32_t LOOP_OSC_INTERVAL_MS = 4;
    // Call once before HTTP/input dispatch and tick, never from Stop continuation.
    void beginCycle() { loopBudget = LOOP_CYCLE_BUDGET; }
   bool trigger(const InputEvent& event);
#if defined(CHAINPAD_HOST_TEST)
   bool triggerCallModelForTest(const InputEvent& event) { return triggerImpl(event, true); }
#endif
   void tick(uint32_t now);
   void cancelAll();
   uint8_t activeCount() const { return running; }
   EngineStats stats;
 private:
     struct Invocation {
       InvocationFrames frames{};
       uint8_t input = 0, current = 0, next = 0;
       bool pairPending : 1, budgeted : 1, paceOsc : 1, oscPending : 1;
       uint32_t wakeAt = 0, oscTicket = 0;
     } jobs[MAX_RUNNING];
    // Permutation: [0,running) is the FIFO; [running,MAX_RUNNING) is free.
    uint8_t order[MAX_RUNNING];
    uint16_t loopBudget = LOOP_CYCLE_BUDGET;
    uint32_t nextOscTicket = 0, lastLoopOscAt = 0;
    bool loopOscClockValid = false;
   uint8_t running = 0;
    enum class AdvanceResult { Complete, Waiting, StopOthers };
     bool triggerImpl(const InputEvent& event, bool supportsCall);
     AdvanceResult advance(Invocation& job, uint32_t now, uint8_t& slice);
     bool admitLoopOsc(Invocation& job, uint32_t now);
     SendResult runAction(const Action&, uint8_t input, uint8_t index, uint32_t now);
     AdvanceResult resume(uint8_t pos, uint32_t now, uint8_t slice);
};
extern Engine engine;
}
