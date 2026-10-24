#pragma once
#include "model.h"

namespace chimera {
bool pairedActionPending(const Action&);
enum class SendResult { Accepted, Unavailable, Busy, Failed };
struct BackendStatus {
  bool usbMidi, usbKeyboard, bleMidi, bleKeyboard, wifi;
  uint32_t retries, overflows;
};
// Backend entry points run on the Arduino loop. OBS delegates socket ownership
// to its worker; these calls never wait for that worker or network completion.
void backendsBegin();
void backendsTick(uint32_t now);
void backendsPanic();
SendResult dispatch(const Action& action, uint32_t now);
BackendStatus backendStatus();
} // namespace chimera
