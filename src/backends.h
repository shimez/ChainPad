#pragma once
#include "model.h"

namespace chimera {
enum class SendResult { Accepted, Unavailable, Busy, Failed };
struct BackendStatus {
  bool usbMidi, usbKeyboard, bleMidi, bleKeyboard, wifi;
  uint32_t retries, overflows;
};
// All transport ownership lives here; called only from the Arduino loop.
void backendsBegin();
void backendsTick(uint32_t now);
void backendsPanic();
SendResult dispatch(const Action& action, uint32_t now);
BackendStatus backendStatus();
} // namespace chimera
