#pragma once
#include "factory_reset_flow.h"

namespace chimera {
ResetBoot factoryResetBoot();
bool requestFactoryReset();
bool factoryResetBlocked();
const char* factoryResetStatus();
}
