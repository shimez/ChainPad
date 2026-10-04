#include "backend_internal.h"

namespace chimera {
namespace {
struct Message { uint8_t status, data1, data2; };
constexpr unsigned MIDI_CAPACITY = MAX_TOTAL_ACTIONS + 64;
struct State {
  Outbox<Message, MIDI_CAPACITY> outbox;
  uint32_t epoch = 0;
  bool connected = false;
} states[2];
void sync(unsigned i) {
  auto& s = states[i]; auto t = transportAt(i);
  bool ready = midiReady(t); uint32_t epoch = midiEpoch(t);
  if (ready != s.connected || epoch != s.epoch) {
    s.outbox.clear(); s.connected = ready; s.epoch = epoch;
  }
}
void flush(unsigned i) {
  sync(i);
  auto& s = states[i]; auto t = transportAt(i);
  if (!s.connected) return;
  for (unsigned budget = 0; budget < 8; ++budget) {
    if (s.outbox.size) {
      const auto& m = s.outbox.front();
      if (!midiWrite(t, m.status, m.data1, m.data2)) { ++transportRetries; return; }
      s.outbox.pop();
    } else break;
  }
}
}
SendResult midiDispatch(const Action& a) {
  if (allNotes(a.message)) {
    // Snapshot target membership at dispatch, independently for each transport.
    uint8_t targets[2][16][16]{};
    unsigned count[2]{};
    for (const auto& chain : config.chains) for (uint8_t k = 0; k < chain.count; ++k) {
      const auto& source = chain.actions[k];
      if (source.protocol != Protocol::Midi || (source.message != MidiMessage::NoteOn && source.message != MidiMessage::NoteOff)) continue;
      for (unsigned i = 0; i < 2; ++i) {
        if (source.transport != Transport::Both && source.transport != transportAt(i)) continue;
        auto& bits = targets[i][source.channel - 1][source.number / 8];
        const uint8_t mask = 1u << (source.number % 8);
        if (!(bits & mask)) { bits |= mask; ++count[i]; }
      }
    }
    bool accepted = false, failed = false;
    for (unsigned i = 0; i < 2; ++i) {
      sync(i); auto& s = states[i];
      if (!count[i] || !s.connected) continue;
      // Reserve the complete batch before appending: never emit a partial prefix.
      if (s.outbox.size + count[i] > MIDI_CAPACITY) { ++transportOverflows; failed = true; continue; }
      for (unsigned ch = 0; ch < 16; ++ch) for (unsigned note = 0; note < 128; ++note) {
        if (targets[i][ch][note / 8] & (1u << (note % 8)))
          s.outbox.push({static_cast<uint8_t>((a.message == MidiMessage::AllNotesOn ? 0x90 : 0x80) | ch),
                        static_cast<uint8_t>(note), static_cast<uint8_t>(a.message == MidiMessage::AllNotesOn ? a.value : 0)});
      }
      accepted = true; flush(i);
    }
    if (accepted || (!count[0] && !count[1])) return SendResult::Accepted;
    return failed ? SendResult::Failed : SendResult::Unavailable;
  }
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
    // Drop this transport's backlog without synthesizing MIDI messages.
    s.outbox.clear(); ++transportOverflows;
    return SendResult::Failed;
  }
  flush(i);
  return SendResult::Accepted;
}
void midiTick() { flush(0); flush(1); }
void midiPanic() { for (auto& s : states) s.outbox.clear(); }
}
