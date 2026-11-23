#include "engine.h"
#include "chain_call.h"
#include "hid_trace.h"
#include "rotation_runtime.h"

namespace chimera {
Engine engine;
Engine::Engine() {
  for (uint8_t i = 0; i < MAX_RUNNING; ++i) order[i] = i;
}
// Tickets only exist while an invocation waits at an OSC Action. Cancelled
// jobs disappear from the scan, so Stop/Panic cannot leave a hole in the FIFO.
// At most 32 tickets coexist; signed differences also order counter wrap.
bool Engine::admitLoopOsc(Invocation& job, uint32_t now) {
  if (!job.oscPending) {
    job.oscTicket = nextOscTicket++;
    job.oscPending = true;
  }
  for (uint8_t i = 0; i < running; ++i)
    if (jobs[order[i]].oscPending && static_cast<int32_t>(jobs[order[i]].oscTicket - job.oscTicket) < 0)
      return false;
  if (loopOscClockValid && uint32_t(now - lastLoopOscAt) < LOOP_OSC_INTERVAL_MS) return false;
  lastLoopOscAt = now; loopOscClockValid = true; job.oscPending = false;
  return true;
}
Engine::AdvanceResult Engine::advance(Invocation& job, uint32_t now, uint8_t& slice) {
  if (job.pairPending) {
    if (pairedActionPending(activeConfig().chains[job.current].actions[job.next - 1])) return AdvanceResult::Waiting;
    job.pairPending = false;
  }
  for (;;) {
    const auto& chain = activeConfig().chains[job.current];
    const bool atEnd = job.next == chain.count;
    if (atEnd && !job.frames.depth) return AdvanceResult::Complete;
    if (job.budgeted) {
      if (!slice || !loopBudget) { job.wakeAt = now; return AdvanceResult::Waiting; }
      if (!atEnd && job.paceOsc && chain.actions[job.next].protocol == Protocol::Osc && !admitLoopOsc(job, millis())) {
        // Keep next, counters and send statistics unchanged. Do not reserve an
        // output packet or spin here: another main-loop cycle will retry admission.
        job.wakeAt = now;
        return AdvanceResult::Waiting;
      }
      --slice; --loopBudget;
    }
    if (atEnd) {
      if (!job.frames.restore(job.current, job.next)) return AdvanceResult::Complete;
      continue;
    }
    const uint8_t index = job.next++;
    const auto& action = chain.actions[index];
    if (action.protocol == Protocol::LoopStart) {
      if (!job.frames.enterLoop(action.loopCount, job.next)) return AdvanceResult::Complete;
      continue;
    }
    if (action.protocol == Protocol::LoopEnd) {
      if (!job.frames.endLoop(job.next)) return AdvanceResult::Complete;
      continue;
    }
    if (action.protocol == Protocol::CallChain) {
      if (action.targetInput >= INPUT_COUNT || !job.frames.suspend(job.current, job.next))
        return AdvanceResult::Complete;
      job.current = action.targetInput; job.next = 0;
      continue;
    }
    const bool wait = action.protocol == Protocol::Wait;
    const bool stop = action.protocol == Protocol::StopOtherChains;
    auto result = runAction(action, job.current, index, now);
    if (stop) return AdvanceResult::StopOthers;
    if (wait && action.delayMs) { job.wakeAt = now + action.delayMs; return AdvanceResult::Waiting; }
    if (pairedAction(action) && result == SendResult::Accepted && pairedActionPending(action)) {
      job.pairPending = true; job.wakeAt = now; return AdvanceResult::Waiting;
    }
  }
}
SendResult Engine::runAction(const Action& action, uint8_t input, uint8_t index, uint32_t now) {
  if (action.protocol == Protocol::Keyboard && action.keyMessage == KeyMessage::Consumer)
    hidtrace::record(hidtrace::Dispatch, action.transport == Transport::Usb ? 0 : 1, action.usage, input, 0, index);
  auto result = (action.protocol == Protocol::Wait || action.protocol == Protocol::StopOtherChains)
    ? SendResult::Accepted : dispatch(action, now);
  stats.lastInput = input; stats.lastAction = index; stats.lastResult = result;
  if (result == SendResult::Accepted) ++stats.accepted; else ++stats.skipped;
  return result;
}
Engine::AdvanceResult Engine::resume(uint8_t pos, uint32_t now, uint8_t slice) {
  const uint8_t id = order[pos];
  const auto result = advance(jobs[id], now, slice);
  if (result == AdvanceResult::StopOthers) {
    stats.cancelledChains += running - 1;
    order[pos] = order[0]; order[0] = id;
    running = 1;
    AdvanceResult tail;
    do { tail = advance(jobs[id], now, slice); } while (tail == AdvanceResult::StopOthers);
    if (tail == AdvanceResult::Complete) running = 0;
    return result; // Old queue positions are invalid even if self completed.
  }
  if (result == AdvanceResult::Complete) {
    --running;
    for (uint8_t j = pos; j < running; ++j) order[j] = order[j + 1];
    order[running] = id;
  }
  return result;
}
bool Engine::trigger(const InputEvent& event) {
  return triggerImpl(event, calls::ENGINE_SUPPORTS_CALL);
}
bool Engine::triggerImpl(const InputEvent& event, bool supportsCall) {
  if (event.input >= INPUT_COUNT) return false;
  ++stats.events;
  if (event.input >= 26 && activeConfig().encoderRotation.mode == RotationMode::RotationValue)
    return rotationRuntime.step(event.input == 26);
  const auto& chain = activeConfig().chains[event.input];
  loops::Layout layout; String error;
  if (!loops::analyze(chain, layout, error, eventActionLimit(event.input))) return false;
  const bool paced = layout.count || (activeConfig().loopReachable & (uint32_t(1) << event.input));
  bool needsSlot = false, stopBeforeWait = false, hasCall = false;
  for (uint8_t i = 0; i < chain.count; ++i) {
    const auto& a = chain.actions[i];
    if (!needsSlot && a.protocol == Protocol::StopOtherChains) stopBeforeWait = true;
    if (a.protocol == Protocol::CallChain) {
      if (!supportsCall) return false;
      hasCall = needsSlot = true;
    }
    if (a.protocol == Protocol::LoopStart ||
        (a.protocol == Protocol::Wait && (a.delayMs || paced)) || pairedAction(a)) needsSlot = true;
  }
  // Reject the whole new invocation before emitting any prefix Actions.
  // Chains without Wait or paired sends remain executable when slots are full.
  if (needsSlot && !stopBeforeWait && running == MAX_RUNNING) {
    hidtrace::record(hidtrace::ChainRejected, 255, event.input);
    ++stats.rejectedChains; stats.lastInput = event.input;
    stats.lastAction = 0; stats.lastResult = SendResult::Busy; return false;
  }
  // Wait duration starts at execution, not at a possibly queued physical edge.
  const uint32_t now = millis();
  // A full scheduler's straight-line Stop prefix must execute atomically even
  // when the cycle budget is exhausted. It is bounded by MAX_ACTIONS; charge
  // its work as budget debt (no budget reset), then yield at the first Header.
  uint8_t next = 0, slice = LOOP_SLICE_BUDGET;
  if (running == MAX_RUNNING) {
    // Admission above guarantees no suspending Action before the first Stop
    // (or end). Execute just that prefix without a 33rd Invocation.
    while (next < chain.count) {
      const uint8_t index = next++;
      const auto& a = chain.actions[index];
      runAction(a, event.input, index, now);
      if (a.protocol == Protocol::StopOtherChains) {
        cancelAll();
        if (layout.count || hasCall) {
          loopBudget = loopBudget > next ? loopBudget - next : 0;
          slice = slice > next ? slice - next : 0;
        }
        break;
      }
    }
    if (next == chain.count) return true;
  }
  const uint8_t pos = running++;
  auto& job = jobs[order[pos]];
  job.current = job.input = event.input; job.next = next; job.frames.reset();
  job.paceOsc = paced; job.budgeted = paced || hasCall;
  job.pairPending = false; job.oscPending = false;
  // Stack entries and ticket are written before use. No stale wake deadline.
  job.wakeAt = now;
  resume(pos, now, slice);
  return true;
}
void Engine::tick(uint32_t now) {
  for (uint8_t i = 0; i < running;) {
    const auto& job = jobs[order[i]];
    if (static_cast<int32_t>(now - job.wakeAt) < 0) { ++i; continue; }
    // Share the remaining cycle work with later jobs rather than allowing the
    // first long-running loops to starve the tail of the stable queue.
    uint8_t slice = LOOP_SLICE_BUDGET;
    if (job.budgeted) {
      const uint16_t share = loopBudget / (running - i);
      if (share < slice) slice = uint8_t(share);
    }
    const auto result = resume(i, now, slice);
    if (result == AdvanceResult::StopOthers) return;
    if (result == AdvanceResult::Waiting) ++i;
  }
}
void Engine::cancelAll() { stats.cancelledChains += running; running = 0; }
} // namespace chimera
