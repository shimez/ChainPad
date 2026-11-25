#pragma once
#include "model.h"
#include "backends.h"
#include "chain_loop.h"
#include "invocation_frames.h"

namespace chimera {
#if (defined(CONFIG_IDF_TARGET_ESP32C5) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(CHAINPAD_HOST_TEST)
#define CHAINPAD_ENGINE_EXTERNAL 1
#else
#define CHAINPAD_ENGINE_EXTERNAL 0
#endif
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
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
#if CHAINPAD_ENGINE_EXTERNAL
    bool begin();
    bool ready() const { return jobs != nullptr; }
    const void* slotBase() const { return jobs; }
    static constexpr size_t slotBytes() { return sizeof(Invocation) * MAX_RUNNING; }
#else
    bool begin() { return true; }
    bool ready() const { return true; }
#endif
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
       InvocationFrames frames{};
       uint8_t input = 0, current = 0, next = 0;
       bool pairPending : 1, budgeted : 1, paceOsc : 1, oscPending : 1;
       uint32_t wakeAt = 0, oscTicket = 0;
     };
#if CHAINPAD_ENGINE_EXTERNAL
    Invocation* jobs = nullptr;
    bool initAttempted = false;
#else
    Invocation jobs[MAX_RUNNING];
#endif
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
