#include "engine.h"

namespace chimera {
Engine engine;
bool Engine::trigger(const InputEvent& event) {
  if (event.input >= INPUT_COUNT) return false;
  ++stats.events;
  const Chain& chain = config.chains[event.input];
  // Dispatch in registration order. A disconnected protocol does not stop
  // subsequent actions. Transport delivery is asynchronous, not atomic.
  for (uint8_t i = 0; i < chain.count; ++i) {
    auto result = dispatch(chain.actions[i], millis());
    stats.lastInput = event.input; stats.lastAction = i; stats.lastResult = result;
    if (result == SendResult::Accepted) ++stats.accepted; else ++stats.skipped;
  }
  return true;
}
}
