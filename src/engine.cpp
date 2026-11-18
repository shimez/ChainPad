#include "engine.h"
#include "hid_trace.h"
#include "rotation_runtime.h"

namespace chimera {
Engine engine;
// Tickets only exist while an invocation waits at an OSC Action. Cancelled
// jobs disappear from the scan, so Stop/Panic cannot leave a hole in the FIFO.
// At most 32 tickets coexist; signed differences also order counter wrap.
bool Engine::admitLoopOsc(Invocation& job, uint32_t now) {
  if (!job.oscPending) {
    job.oscTicket = nextOscTicket++;
    job.oscPending = true;
  }
  for (uint8_t i = 0; i < running; ++i)
    if (jobs[i].oscPending && static_cast<int32_t>(jobs[i].oscTicket - job.oscTicket) < 0)
      return false;
  if (loopOscClockValid && uint32_t(now - lastLoopOscAt) < LOOP_OSC_INTERVAL_MS) return false;
  lastLoopOscAt = now; loopOscClockValid = true; job.oscPending = false;
  return true;
}
Engine::AdvanceResult Engine::advance(Invocation& job, uint32_t now, uint8_t& slice) {
  const auto& chain = activeConfig().chains[job.input];
  if (job.pairPending) {
    if (pairedActionPending(chain.actions[job.next - 1])) return AdvanceResult::Waiting;
    job.pairPending = false;
  }
  while (job.next < chain.count) {
    if (job.looping) {
      if (!slice || !loopBudget) { job.wakeAt = now; return AdvanceResult::Waiting; }
      if (chain.actions[job.next].protocol == Protocol::Osc && !admitLoopOsc(job, millis())) {
        // Keep next, counters and send statistics unchanged. Do not reserve an
        // output packet or spin here: another main-loop cycle will retry admission.
        job.wakeAt = now;
        return AdvanceResult::Waiting;
      }
      --slice; --loopBudget;
    }
    const uint8_t index = job.next++;
    const auto& action = chain.actions[index];
    if (action.protocol == Protocol::LoopStart) {
      job.stack[job.depth++] = {uint16_t(action.loopCount), job.next};
      continue;
    }
    if (action.protocol == Protocol::LoopEnd) {
      auto& frame = job.stack[job.depth - 1];
      if (--frame.remaining) job.next = frame.start;
      else --job.depth;
      continue;
    }
    if (action.protocol == Protocol::Keyboard && action.keyMessage == KeyMessage::Consumer)
      hidtrace::record(hidtrace::Dispatch, action.transport == Transport::Usb ? 0 : 1, action.usage, job.input, 0, index);
    const bool wait = action.protocol == Protocol::Wait;
    const bool stop = action.protocol == Protocol::StopOtherChains;
    auto result = (wait || stop) ? SendResult::Accepted : dispatch(action, now);
    stats.lastInput = job.input; stats.lastAction = index; stats.lastResult = result;
    if (result == SendResult::Accepted) ++stats.accepted; else ++stats.skipped;
    if (stop) return AdvanceResult::StopOthers;
    if (wait && action.delayMs) { job.wakeAt = now + action.delayMs; return AdvanceResult::Waiting; }
    if (pairedAction(action) && result == SendResult::Accepted && pairedActionPending(action)) {
      job.pairPending = true; job.wakeAt = now; return AdvanceResult::Waiting;
    }
  }
  return AdvanceResult::Complete;
}
// Called only after advance() has returned and the caller owns a local job.
// No backend queues, held outputs, rotation state or input events are touched.
void Engine::continueAfterStop(Invocation& job, uint32_t now, uint8_t others, uint8_t& slice) {
  stats.cancelledChains += others;
  running = 0;
  AdvanceResult result;
  do { result = advance(job, now, slice); } while (result == AdvanceResult::StopOthers);
  if (result == AdvanceResult::Waiting) jobs[running++] = job;
}
bool Engine::trigger(const InputEvent& event) {
  if (event.input >= INPUT_COUNT) return false;
  ++stats.events;
  if (event.input >= 26 && activeConfig().encoderRotation.mode == RotationMode::RotationValue)
    return rotationRuntime.step(event.input == 26);
  const auto& chain = activeConfig().chains[event.input];
  loops::Layout layout; String error;
  if (!loops::analyze(chain, layout, error, eventActionLimit(event.input))) return false;
  bool needsSlot = false, stopBeforeWait = false;
  for (uint8_t i = 0; i < chain.count; ++i) {
    const auto& a = chain.actions[i];
    if (!needsSlot && a.protocol == Protocol::StopOtherChains) stopBeforeWait = true;
    if (a.protocol == Protocol::LoopStart ||
        (a.protocol == Protocol::Wait && (a.delayMs || layout.count)) || pairedAction(a)) needsSlot = true;
  }
  // Reject the whole new invocation before emitting any prefix Actions.
  // Chains without Wait or paired sends remain executable when slots are full.
  if (needsSlot && !stopBeforeWait && running == MAX_RUNNING) {
    hidtrace::record(hidtrace::ChainRejected, 255, event.input);
    ++stats.rejectedChains; stats.lastInput = event.input;
    stats.lastAction = 0; stats.lastResult = SendResult::Busy; return false;
  }
  Invocation job; job.input = event.input;
  job.looping = layout.count != 0;
  // Wait duration starts at execution, not at a possibly queued physical edge.
  const uint32_t now = millis();
  // A full scheduler's straight-line Stop prefix must execute atomically even
  // when the cycle budget is exhausted. It is bounded by MAX_ACTIONS; charge
  // its work as budget debt (no budget reset), then yield at the first Header.
  if (job.looping && running == MAX_RUNNING && stopBeforeWait) {
    job.looping = false;
    uint8_t prefix = LOOP_SLICE_BUDGET;
    const auto result = advance(job, now, prefix);
    if (result == AdvanceResult::StopOthers) {
      const uint8_t used = job.next;
      loopBudget = loopBudget > used ? loopBudget - used : 0;
      job.looping = true;
      uint8_t slice = used < LOOP_SLICE_BUDGET ? LOOP_SLICE_BUDGET - used : 0;
      continueAfterStop(job, now, running, slice);
    }
    return true;
  }
  uint8_t slice = LOOP_SLICE_BUDGET;
  const auto result = advance(job, now, slice);
  if (result == AdvanceResult::StopOthers) continueAfterStop(job, now, running, slice);
  else if (result == AdvanceResult::Waiting) jobs[running++] = job;
  return true;
}
void Engine::tick(uint32_t now) {
  for (uint8_t i = 0; i < running;) {
    if (static_cast<int32_t>(now - jobs[i].wakeAt) < 0) { ++i; continue; }
    // Share the remaining cycle work with later jobs rather than allowing the
    // first long-running loops to starve the tail of the stable queue.
    uint8_t slice = LOOP_SLICE_BUDGET;
    if (jobs[i].looping) {
      const uint16_t share = loopBudget / (running - i);
      if (share < slice) slice = uint8_t(share);
    }
    const auto result = advance(jobs[i], now, slice);
    if (result == AdvanceResult::StopOthers) {
      Invocation job = jobs[i]; // Preserve self before invalidating the array.
      continueAfterStop(job, now, running - 1, slice);
      return; // All old indices are invalid; self has already continued once.
    }
    if (result == AdvanceResult::Waiting) { ++i; continue; }
    // Stable order for simultaneous resumptions; retriggers are independent.
    --running;
    for (uint8_t j = i; j < running; ++j) jobs[j] = jobs[j + 1];
  }
}
void Engine::cancelAll() { stats.cancelledChains += running; running = 0; }
} // namespace chimera
