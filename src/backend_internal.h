#pragma once
#include "backends.h"
#include "transports.h"

namespace chimera {
extern uint32_t transportRetries, transportOverflows;
SendResult oscDispatch(const Action&);
SendResult midiDispatch(const Action&);
SendResult keyboardDispatch(const Action&);
void midiTick();
bool midiQueued(Transport);
void keyboardTick();
void midiPanic();
void keyboardPanic();
// FIFO ownership is confined to the loop task. BLE callbacks only set atomics.
template<typename T, unsigned Capacity = 64> struct Outbox {
  T entries[Capacity]{};
  unsigned head = 0, size = 0;
  bool push(const T& value) {
    if (size == Capacity) return false;
    entries[(head + size++) % Capacity] = value; return true;
  }
  const T& front() const { return entries[head]; }
  void pop() { head = (head + 1) % Capacity; --size; }
  void clear() { head = size = 0; }
};
inline Transport transportAt(unsigned i) { return i == 0 ? Transport::Usb : Transport::Ble; }
inline unsigned transportIndex(Transport t) { return t == Transport::Usb ? 0 : 1; }
}
