#pragma once
#include "obs_backend.h"
namespace chimera {
// Loop-owned admission diagnostics, separate from asynchronous completion.
struct ObsActionStatus {
  const char* admission = "notRequested";
  uint32_t requestId = 0, completed = 0, failed = 0;
  ObsResult last;
};
SendResult obsActionDispatch(const Action&);
void obsActionsTick();
const ObsActionStatus& obsActionStatus();
}
