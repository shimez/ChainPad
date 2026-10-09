#include "backend_internal.h"
#include "rotation_sender.h"
#include <WiFi.h>

namespace chimera {
uint32_t transportRetries = 0, transportOverflows = 0;
void backendsBegin() { transportsBegin(); rotationSenderBegin(); }
void backendsTick(uint32_t) { transportsTick(); midiTick(); keyboardTick(); }
void backendsPanic() { midiPanic(); keyboardPanic(); rotationDiscard(); }
bool pairedActionPending(const Action& a) {
  if (a.protocol == Protocol::Keyboard) return keyboardQueued(a.transport);
  if (a.transport == Transport::Both || a.message == MidiMessage::AllNotesOnOff) return midiQueued(Transport::Usb) || midiQueued(Transport::Ble);
  return midiQueued(a.transport);
}
SendResult dispatch(const Action& a, uint32_t) {
  switch (a.protocol) {
    case Protocol::Osc: return oscDispatch(a);
    case Protocol::Midi: return midiDispatch(a);
    case Protocol::Keyboard: return keyboardDispatch(a);
    case Protocol::Wait: return SendResult::Failed; // Owned by the Chain scheduler.
  }
  return SendResult::Failed;
}
BackendStatus backendStatus() {
  return {midiReady(Transport::Usb), keyboardReady(Transport::Usb),
    midiReady(Transport::Ble), keyboardReady(Transport::Ble),
    WiFi.status() == WL_CONNECTED, transportRetries, transportOverflows};
}
}
