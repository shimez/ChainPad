#pragma once
#include "obs_backend.h"
namespace chimera {
// Loop-only discovery cache. The Action adapter remains the sole result consumer.
SendResult obsScenesRefresh();
void obsScenesJson(JsonObject);
SendResult obsInputsRefresh();
void obsInputsJson(JsonObject);
}
