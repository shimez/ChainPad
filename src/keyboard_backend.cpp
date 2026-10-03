#include "backend_internal.h"

namespace chimera {
namespace {
struct State {
  Outbox<KeyboardReport> outbox;
  uint8_t held[116]{};
  uint8_t modifiers[116]{};
  bool connected = false, reset = true;
  uint32_t epoch = 0;
  void clear() { memset(held, 0, sizeof(held)); memset(modifiers, 0, sizeof(modifiers)); outbox.clear(); reset = true; }
} states[2];
void sync(unsigned i) {
  auto& s = states[i]; auto t = transportAt(i);
  bool ready = keyboardReady(t); uint32_t epoch = keyboardEpoch(t);
  if (s.connected != ready || s.epoch != epoch) {
    s.clear(); s.connected = ready; s.epoch = epoch;
  }
}
void flush(unsigned i) {
  sync(i);
  auto& s = states[i]; auto t = transportAt(i);
  if (!s.connected) return;
  if (s.reset) {
    if (!keyboardWrite(t, KeyboardReport{})) { ++transportRetries; return; }
    s.reset = false;
    return;
  }
  if (s.outbox.size) {
    if (!keyboardWrite(t, s.outbox.front())) { ++transportRetries; return; }
    s.outbox.pop();
  }
}
}
SendResult keyboardDispatch(const Action& a) {
  unsigned i = transportIndex(a.transport); sync(i);
  auto& s = states[i];
  if (!s.connected) return SendResult::Unavailable;
  if (a.keyMessage == KeyMessage::ReleaseAll) {
    memset(s.held, 0, sizeof(s.held)); memset(s.modifiers, 0, sizeof(s.modifiers));
  } else {
    if (a.keyMessage == KeyMessage::Down && !s.held[a.usage]) {
      unsigned count = 0; for (auto h : s.held) count += h != 0;
      if (count == 6) return SendResult::Busy;
    }
    s.held[a.usage] = a.keyMessage == KeyMessage::Down;
    s.modifiers[a.usage] = a.keyMessage == KeyMessage::Down ? a.modifiers : 0;
  }
  KeyboardReport report;
  unsigned slot = 0;
  for (unsigned key = 4; key < 116; ++key) if (s.held[key]) {
    report.keys[slot++] = key; report.modifiers |= s.modifiers[key];
  }
  if (!s.outbox.push(report)) { s.clear(); ++transportOverflows; return SendResult::Failed; }
  flush(i);
  return SendResult::Accepted;
}
void keyboardTick() { flush(0); flush(1); }
void keyboardPanic() { for (auto& s : states) s.clear(); }
}
