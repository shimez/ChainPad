#pragma once
#include "capabilities.h"
#if defined(CHAINPAD_OBS_TEST)
#if !CHAINPAD_HAS_OBS
#error "OBS test console requires the S3 backend"
#endif
#include <Arduino.h>
namespace chimera { bool obsTestByte(int value, Print& out); }
#endif
