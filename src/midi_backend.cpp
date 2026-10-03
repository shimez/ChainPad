#include "backend_internal.h"

namespace chimera {
namespace {
struct Message { uint8_t status, data1, data2; };
struct State {
  Outbox<Message> outbox;
  uint32_t epoch = 0;
  bool connected = false;
  uint8_t reset = 0; // 48 reset CCs: sustain off, all sound off, all notes off.
} states[2];
void sync(unsigned i) {
  auto& s = states[i]; auto t = transportAt(i);
  bool ready = midiReady(t); uint32_t epoch = midiEpoch(t);
  if (ready != s.connected || epoch != s.epoch) {
    s.outbox.clear(); s.reset = 0; s.connected = ready; s.epoch = epoch;
  }
}
void flush(unsigned i) {
  sync(i);
  auto& s = states[i]; auto t = transportAt(i);
  if (!s.connected) return;
  const uint8_t resetCC[] = {64, 120, 123};
  for (unsigned budget = 0; budget < 8; ++budget) {
    if (s.reset < 48) {
      if (!midiWrite(t, 0xb0 | (s.reset / 3), resetCC[s.reset % 3], 0)) { ++transportRetries; return; }
      ++s.reset;
    } else if (s.outbox.size) {
      const auto& m = s.outbox.front();
      if (!midiWrite(t, m.status, m.data1, m.data2)) { ++transportRetries; return; }
      s.outbox.pop();
    } else break;
  }
}
}
SendResult midiDispatch(const Action& a) {
  if (a.transport == Transport::Both) {
    Action routed = a;
    routed.transport = Transport::Usb;
    const auto usb = midiDispatch(routed);
    routed.transport = Transport::Ble;
    const auto ble = midiDispatch(routed);
    // Each transport owns its FIFO/retries: never resend the successful
    // output merely because the other output was unavailable or failed.
    if (usb == SendResult::Accepted || ble == SendResult::Accepted) return SendResult::Accepted;
    if (usb == SendResult::Failed || ble == SendResult::Failed) return SendResult::Failed;
    return SendResult::Unavailable;
  }
  unsigned i = transportIndex(a.transport); sync(i);
  auto& s = states[i];
  if (!s.connected) return SendResult::Unavailable;
  uint8_t status = a.message == MidiMessage::NoteOn ? 0x90 : a.message == MidiMessage::NoteOff ? 0x80 : 0xb0;
  Message m{static_cast<uint8_t>(status | (a.channel - 1)), a.number, a.value};
  if (!s.outbox.push(m)) {
    // A lost Note Off must not leave a stuck voice. Drop this transport's
    // backlog and schedule a panic before accepting subsequent actions.
    s.outbox.clear(); s.reset = 0; ++transportOverflows;
    return SendResult::Failed;
  }
  flush(i);
  return SendResult::Accepted;
}
void midiTick() { flush(0); flush(1); }
void midiPanic() { for (auto& s : states) { s.outbox.clear(); s.reset = 0; } }
}
