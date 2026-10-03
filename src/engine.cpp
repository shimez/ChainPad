#include "engine.h"

namespace chimera {
Engine engine;
bool Engine::advance(Invocation& job, uint32_t now) {
  const auto& chain = config.chains[job.input];
  while (job.next < chain.count) {
    const uint8_t index = job.next++;
    const auto& action = chain.actions[index];
    const bool wait = action.protocol == Protocol::Wait;
    auto result = wait ? SendResult::Accepted : dispatch(action, now);
    stats.lastInput = job.input; stats.lastAction = index; stats.lastResult = result;
    if (result == SendResult::Accepted) ++stats.accepted; else ++stats.skipped;
    if (wait && action.delayMs) { job.wakeAt = now + action.delayMs; return false; }
  }
  return true;
}
bool Engine::trigger(const InputEvent& event) {
  if (event.input >= INPUT_COUNT) return false;
  ++stats.events;
  const auto& chain = config.chains[event.input];
  bool needsSlot = false;
  for (uint8_t i = 0; i < chain.count; ++i)
    if (chain.actions[i].protocol == Protocol::Wait && chain.actions[i].delayMs) needsSlot = true;
  // Reject the whole new invocation before emitting any prefix Actions.
  // Immediate chains (notably Release) remain executable when wait slots are full.
  if (needsSlot && running == MAX_RUNNING) {
    ++stats.rejectedChains; stats.lastInput = event.input;
    stats.lastAction = 0; stats.lastResult = SendResult::Busy; return false;
  }
  Invocation job; job.input = event.input;
  // Wait duration starts at execution, not at a possibly queued physical edge.
  if (!advance(job, millis())) jobs[running++] = job;
  return true;
}
void Engine::tick(uint32_t now) {
  for (uint8_t i = 0; i < running;) {
    if (static_cast<int32_t>(now - jobs[i].wakeAt) < 0 || !advance(jobs[i], now)) { ++i; continue; }
    // Stable order for simultaneous resumptions; retriggers are independent.
    --running;
    for (uint8_t j = i; j < running; ++j) jobs[j] = jobs[j + 1];
  }
}
void Engine::cancelAll() { stats.cancelledChains += running; running = 0; }
} // namespace chimera
