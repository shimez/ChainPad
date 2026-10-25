#pragma once
#include "capabilities.h"
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
#include <WebServer.h>
#include "obs_control.h"
namespace chimera {
// Callbacks are sampled on the same loop as WebServer and Store.
void registerObsHttp(WebServer& web, obscontrol::Controller& control,
                     bool (*maintenance)(), bool (*outputsAllowed)());
}
#endif
