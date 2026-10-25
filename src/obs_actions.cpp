#include "obs_actions.h"
#include <cstring>
namespace chimera {
namespace { ObsActionStatus status; }
const ObsActionStatus& obsActionStatus() { return status; }
SendResult obsActionDispatch(const Action& a) {
  status.requestId = 0;
#if !CHAINPAD_HAS_OBS
  (void)a; status.admission = "unsupported"; return SendResult::Unavailable;
#elif defined(CHAINPAD_OBS_TEST)
  // Test console retains exclusive ownership of its settings and result FIFO.
  (void)a; status.admission = "testConsoleOwned"; return SendResult::Unavailable;
#else
  obs::Command command; command.operation = a.obsOperation; command.muted = a.boolValue;
  if (obs::named(command.operation)) memcpy(command.target, a.obsTarget, sizeof(command.target));
  const auto result = obsDispatch(command, status.requestId);
  switch (result) {
    case SendResult::Accepted: status.admission = "accepted"; break;
    case SendResult::Busy: status.admission = "busy"; break;
    case SendResult::Failed: status.admission = "failed"; break;
    case SendResult::Unavailable:
      status.admission = obsStatusSnapshot().state == obs::State::Disabled ? "disabled" : "disconnected"; break;
  }
  return result;
#endif
}
void obsActionsTick() {
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
  ObsResult result;
  // Bounded work per loop, sufficient to empty the existing eight-entry FIFO.
  for (unsigned n = 0; n < 8 && obsNextResult(result); ++n) {
    status.last = result;
    if (result.success) ++status.completed; else ++status.failed;
  }
#endif
}
}
