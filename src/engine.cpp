#include "engine.h"
#include "hid_trace.h"
#include "rotation_runtime.h"

namespace chimera {
Engine engine;
Engine::AdvanceResult Engine::advance(Invocation& job, uint32_t now) {
  const auto& chain = activeConfig().chains[job.input];
  if (job.pairPending) {
    if (pairedActionPending(chain.actions[job.next - 1])) return AdvanceResult::Waiting;
    job.pairPending = false;
  }
  while (job.next < chain.count) {
    const uint8_t index = job.next++;
    const auto& action = chain.actions[index];
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
void Engine::continueAfterStop(Invocation& job, uint32_t now, uint8_t others) {
  stats.cancelledChains += others;
  running = 0;
  AdvanceResult result;
  do { result = advance(job, now); } while (result == AdvanceResult::StopOthers);
  if (result == AdvanceResult::Waiting) jobs[running++] = job;
}
bool Engine::trigger(const InputEvent& event) {
  if (event.input >= INPUT_COUNT) return false;
  ++stats.events;
  if (event.input >= 26 && activeConfig().encoderRotation.mode == RotationMode::RotationValue)
    return rotationRuntime.step(event.input == 26);
  const auto& chain = activeConfig().chains[event.input];
  bool needsSlot = false, stopBeforeWait = false;
  for (uint8_t i = 0; i < chain.count; ++i) {
    const auto& a = chain.actions[i];
    if (!needsSlot && a.protocol == Protocol::StopOtherChains) stopBeforeWait = true;
    if ((a.protocol == Protocol::Wait && a.delayMs) || pairedAction(a)) needsSlot = true;
  }
  // Reject the whole new invocation before emitting any prefix Actions.
  // Chains without Wait or paired sends remain executable when slots are full.
  if (needsSlot && !stopBeforeWait && running == MAX_RUNNING) {
    hidtrace::record(hidtrace::ChainRejected, 255, event.input);
    ++stats.rejectedChains; stats.lastInput = event.input;
    stats.lastAction = 0; stats.lastResult = SendResult::Busy; return false;
  }
  Invocation job; job.input = event.input;
  // Wait duration starts at execution, not at a possibly queued physical edge.
  const uint32_t now = millis();
  const auto result = advance(job, now);
  if (result == AdvanceResult::StopOthers) continueAfterStop(job, now, running);
  else if (result == AdvanceResult::Waiting) jobs[running++] = job;
  return true;
}
void Engine::tick(uint32_t now) {
  for (uint8_t i = 0; i < running;) {
    if (static_cast<int32_t>(now - jobs[i].wakeAt) < 0) { ++i; continue; }
    const auto result = advance(jobs[i], now);
    if (result == AdvanceResult::StopOthers) {
      Invocation job = jobs[i]; // Preserve self before invalidating the array.
      continueAfterStop(job, now, running - 1);
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
