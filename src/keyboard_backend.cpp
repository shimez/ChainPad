#include "backend_internal.h"
#include "hid_trace.h"

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
struct ConsumerState {
  Outbox<uint16_t> outbox;
  bool connected = false, reset = true;
  uint32_t epoch = 0;
#if CHAINPAD_HID_TRACE
  uint32_t ids[64]{}; // Sidecar only; original FIFO/report layout is unchanged.
#endif
  uint32_t frontId() const {
#if CHAINPAD_HID_TRACE
    return ids[outbox.head];
#else
    return 0;
#endif
  }
  void push(unsigned i, uint16_t usage) {
#if CHAINPAD_HID_TRACE
    const uint32_t id = hidtrace::nextId();
    ids[(outbox.head + outbox.size) % 64] = id;
#else
    const uint32_t id = 0;
#endif
    outbox.push(usage);
    hidtrace::record(hidtrace::Push, i, usage, id, epoch, outbox.size);
  }
  void clear(unsigned i, int reason) {
    hidtrace::record(hidtrace::Reset, i, outbox.size, 0, epoch, reason);
#if CHAINPAD_HID_TRACE
    for (unsigned n = 0; n < outbox.size; ++n) {
      const unsigned slot = (outbox.head + n) % 64;
      hidtrace::record(hidtrace::Discard, i, outbox.entries[slot], ids[slot], epoch, 1);
    }
#endif
    outbox.clear(); reset = true;
  }
} consumers[2];
void syncConsumer(unsigned i) {
  auto& s = consumers[i]; const auto t = transportAt(i);
  const bool ready = consumerReady(t); const auto epoch = consumerEpoch(t);
  if (s.connected != ready || s.epoch != epoch) {
    s.clear(i, 1); s.connected = ready; s.epoch = epoch;
  }
}
void flushConsumer(unsigned i) {
  syncConsumer(i);
  auto& s = consumers[i]; const auto t = transportAt(i);
  if (!s.connected) return;
  if (s.reset) {
    hidtrace::record(hidtrace::Neutral, i, 0, 0, s.epoch);
#if CHAINPAD_HID_TRACE
    hidtrace::context(i, 0);
#endif
    if (!consumerWrite(t, 0)) { ++transportRetries; return; }
    s.reset = false; return;
  }
  if (s.outbox.size) {
#if CHAINPAD_HID_TRACE
    hidtrace::context(i, s.frontId());
#endif
    if (!consumerWrite(t, s.outbox.front())) { ++transportRetries; return; }
    hidtrace::record(hidtrace::Pop, i, s.outbox.front(), s.frontId(), s.epoch, s.outbox.size - 1);
    s.outbox.pop();
  }
}
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
  if (a.keyMessage == KeyMessage::Consumer) {
    if (!consumerUsage(a.usage)) {
      hidtrace::record(hidtrace::ActionRejected, transportIndex(a.transport), a.usage, 0, 0, 1);
      return SendResult::Failed;
    }
    const unsigned i = transportIndex(a.transport); syncConsumer(i);
    auto& s = consumers[i];
    if (!s.connected) {
      hidtrace::record(hidtrace::ActionRejected, i, a.usage, 0, s.epoch, 2);
      return SendResult::Unavailable;
    }
    // Reserve down AND release atomically; never enqueue an orphaned press.
    if (s.outbox.size + 2 > 64) {
      ++transportOverflows;
      hidtrace::record(hidtrace::Overflow, i, a.usage, 0, s.epoch, s.outbox.size);
      hidtrace::record(hidtrace::ActionRejected, i, a.usage, 0, s.epoch, 3);
      return SendResult::Failed;
    }
    hidtrace::record(hidtrace::ActionAccepted, i, a.usage, 0, s.epoch);
    s.push(i, a.usage); s.push(i, 0); flushConsumer(i);
    return SendResult::Accepted;
  }
  if (a.keyMessage == KeyMessage::ReleaseAll) {
    consumers[transportIndex(a.transport)].clear(transportIndex(a.transport), 2);
    flushConsumer(transportIndex(a.transport));
  }
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
void keyboardTick() { flush(0); flush(1); flushConsumer(0); flushConsumer(1); }
bool keyboardQueued(Transport t) { unsigned i = transportIndex(t); sync(i); syncConsumer(i); return states[i].outbox.size != 0 || consumers[i].outbox.size != 0; }
void keyboardPanic() { for (auto& s : states) s.clear(); for (unsigned i = 0; i < 2; ++i) consumers[i].clear(i, 3); }
}
