#include "backend_internal.h"

namespace chimera {
namespace {
struct State {
  Outbox<KeyboardReport> outbox;
  uint8_t held[116]{};
  uint8_t modifiers[116]{};
  bool connected = false, reset = true;
  uint32_t epoch = 0;
  bool waiting = false;
  uint32_t wakeAt = 0;
  void clear() { memset(held, 0, sizeof(held)); memset(modifiers, 0, sizeof(modifiers)); outbox.clear(); reset = true; waiting = false; }
} states[2];
void sync(unsigned i) {
  auto& s = states[i]; auto t = transportAt(i);
  bool ready = keyboardReady(t); uint32_t epoch = keyboardEpoch(t);
  if (s.connected != ready || s.epoch != epoch) {
    s.clear(); s.connected = ready; s.epoch = epoch;
  }
}
bool startHold(State& s) {
  if (!s.outbox.size || s.outbox.front().keys[0] != 0xff) return false;
  const auto marker = s.outbox.front(); s.outbox.pop();
  const uint32_t duration = uint32_t(marker.keys[1]) | (uint32_t(marker.keys[2]) << 8) |
    (uint32_t(marker.keys[3]) << 16) | (uint32_t(marker.keys[4]) << 24);
  s.wakeAt = millis() + duration; s.waiting = true; return true;
}
void flush(unsigned i) {
  sync(i);
  auto& s = states[i]; auto t = transportAt(i);
  if (!s.connected) return;
  if (s.waiting) {
    if (static_cast<int32_t>(millis() - s.wakeAt) < 0) return;
    s.waiting = false;
  }
  if (s.reset) {
    if (!keyboardWrite(t, KeyboardReport{})) { ++transportRetries; return; }
    s.reset = false;
    return;
  }
  if (s.outbox.size) {
    if (startHold(s)) return;
    if (!keyboardWrite(t, s.outbox.front())) { ++transportRetries; return; }
    s.outbox.pop();
    startHold(s);
  }
}
}
SendResult keyboardDispatch(const Action& a) {
  unsigned i = transportIndex(a.transport); sync(i);
  auto& s = states[i];
  if (!s.connected) return SendResult::Unavailable;
  if (a.keyMessage == KeyMessage::DownUp) {
    // Do not release a key already held by another Action.
    if (s.held[a.usage]) return SendResult::Busy;
    KeyboardReport up, down;
    unsigned slot = 0;
    for (unsigned key = 4; key < 116; ++key) if (s.held[key]) {
      up.keys[slot++] = key; up.modifiers |= s.modifiers[key];
    }
    if (slot == 6) return SendResult::Busy;
    if (s.outbox.size + 2 + (a.delayMs ? 1 : 0) > 64) { ++transportOverflows; return SendResult::Failed; }
    down = up; down.keys[slot] = a.usage; down.modifiers |= a.modifiers;
    s.outbox.push(down);
    if (a.delayMs) {
      KeyboardReport hold; hold.keys[0] = 0xff;
      for (unsigned j = 0; j < 4; ++j) hold.keys[j + 1] = uint8_t(a.delayMs >> (8 * j));
      s.outbox.push(hold);
    }
    s.outbox.push(up);
    flush(i);
    return SendResult::Accepted;
  }
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
bool keyboardQueued(Transport t) { unsigned i = transportIndex(t); sync(i); return states[i].outbox.size != 0; }
void keyboardPanic() { for (auto& s : states) s.clear(); }
}
