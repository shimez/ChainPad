#include "model.h"
#include "engine.h"
#include "backend_internal.h"
#include "Preferences.h"
#include "WiFiUdp.h"
#include "WiFi.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace chimera;
namespace fake {
bool connected[2] = {true, true}, writable = true;
uint32_t epoch[2] = {};
struct Midi { Transport transport; uint8_t status, a, b; };
std::vector<Midi> midi;
std::vector<KeyboardReport> hid;
}
namespace chimera {
void transportsBegin() {}
void transportsTick() {}
bool midiReady(Transport t) { return fake::connected[transportIndex(t)]; }
bool keyboardReady(Transport t) { return midiReady(t); }
uint32_t midiEpoch(Transport t) { return fake::epoch[transportIndex(t)]; }
uint32_t keyboardEpoch(Transport t) { return midiEpoch(t); }
bool midiWrite(Transport t, uint8_t status, uint8_t a, uint8_t b) {
  if (!fake::writable) return false;
  fake::midi.push_back({t, status, a, b}); return true;
}
bool keyboardWrite(Transport, const KeyboardReport& report) {
  if (!fake::writable) return false;
  fake::hid.push_back(report); return true;
}
}
void settle() { for (int i = 0; i < 20; ++i) backendsTick(0); }
void reset() {
  fake::writable = true; fake::connected[0] = fake::connected[1] = true;
  ++fake::epoch[0]; ++fake::epoch[1]; settle();
  fake::midi.clear(); fake::hid.clear();
}
String encoded(const Config& source) {
  JsonDocument d; encodeConfig(source, d); String json; serializeJson(d, json); return json;
}
void configTests() {
  Config source;
  Action on; on.protocol = Protocol::Midi; on.transport = Transport::Usb;
  Action off = on; off.message = MidiMessage::NoteOff; off.value = 0;
  source.chains[0].count = source.chains[1].count = 1;
  source.chains[0].actions[0] = on; source.chains[1].actions[0] = off;
  JsonDocument d; encodeConfig(source, d);
  Config out; String error;
  assert(decodeConfig(d.as<JsonVariantConst>(), out, error));
  assert(out.chains[1].actions[0].message == MidiMessage::NoteOff);
  d["chains"][0]["actions"][0]["transport"] = "both";
  assert(decodeConfig(d.as<JsonVariantConst>(), out, error));
  assert(out.chains[0].actions[0].transport == Transport::Both);
  JsonDocument roundtrip; encodeConfig(out, roundtrip);
  assert(roundtrip["chains"][0]["actions"][0]["transport"] == "both");
  d["chains"][0]["actions"][0].remove("transport");
  assert(decodeConfig(d.as<JsonVariantConst>(), out, error));
  assert(out.chains[0].actions[0].transport == Transport::Both);
  d["chains"][0]["actions"][0]["protocol"] = "keyboard";
  d["chains"][0]["actions"][0]["transport"] = "both";
  assert(!decodeConfig(d.as<JsonVariantConst>(), out, error));
  encodeConfig(source, d);
  d["chains"][1]["input"] = 0;
  assert(!decodeConfig(d.as<JsonVariantConst>(), out, error));
  encodeConfig(source, d); d["chains"][0]["actions"][0]["channel"] = 17;
  assert(!decodeConfig(d.as<JsonVariantConst>(), out, error));
  encodeConfig(source, d); d["chains"][0]["actions"][0]["delayMs"] = 200;
  assert(!decodeConfig(d.as<JsonVariantConst>(), out, error));
  encodeConfig(source, d); d["network"]["oscHost"] = "not-an-ip";
  assert(!decodeConfig(d.as<JsonVariantConst>(), out, error));
  assert(saveConfig(encoded(source), error));
  fake::writeFailure = true; source.oscPort = 7777;
  assert(!saveConfig(encoded(source), error)); assert(config.oscPort == 9000);
  fake::writeFailure = false; config.chains[0].count = 0;
  assert(loadConfig(error)); assert(config.chains[0].count == 1);
  // Exercise a multi-page JSON blob (NVS strings cannot store this size).
  for (auto& chain : source.chains) {
    chain.count = MAX_ACTIONS;
    for (auto& action : chain.actions) action = on;
  }
  assert(encoded(source).length() > 4096);
  assert(saveConfig(encoded(source), error));
  config.chains[27].count = 0;
  assert(loadConfig(error)); assert(config.chains[27].count == MAX_ACTIONS);
  assert(!saveConfig(String(std::string(MAX_CONFIG_BYTES + 1, ' ')), error));
  assert(config.chains[27].count == MAX_ACTIONS);
  std::cout << "PASS schema validation / save failure atomicity / reload\n";
}
void oscTests() {
  Action a; strcpy(a.address, "/x"); a.intValue = -2;
  assert(oscDispatch(a) == SendResult::Accepted);
  assert((fake::osc == std::vector<uint8_t>{'/', 'x', 0, 0, ',', 'i', 0, 0, 255, 255, 255, 254}));
  a.oscType = OscType::Float; a.floatValue = 1.5;
  oscDispatch(a); assert(fake::osc[8] == 0x3f && fake::osc[9] == 0xc0);
  a.oscType = OscType::Bool; a.boolValue = true;
  oscDispatch(a); assert(fake::osc.size() == 8 && fake::osc[5] == 'T');
  a.oscType = OscType::String; strcpy(a.stringValue, "abcde");
  oscDispatch(a); assert(fake::osc.size() == 16 && fake::osc[12] == 'e' && fake::osc[15] == 0);
  std::cout << "PASS OSC int/float/bool/string wire encoding\n";
}
void midiTests() {
  reset();
  Action a; a.protocol = Protocol::Midi; a.transport = Transport::Usb;
  midiDispatch(a);
  fake::writable = false;
  a.message = MidiMessage::NoteOff; a.value = 0; midiDispatch(a);
  assert(fake::midi.size() == 1);
  fake::writable = true; settle();
  assert(fake::midi.size() == 2 && fake::midi[0].status == 0x90 && fake::midi[1].status == 0x80);
  fake::connected[0] = false; settle();
  assert(midiDispatch(a) == SendResult::Unavailable);
  fake::midi.clear(); fake::connected[0] = true; ++fake::epoch[0]; settle();
  assert(fake::midi.size() == 48 && fake::midi.back().status == 0xbf && fake::midi.back().a == 123);
  // Overflow must replace a potentially lost Note Off with an all-channel reset.
  fake::writable = false;
  for (int i = 0; i < 64; ++i) assert(midiDispatch(a) == SendResult::Accepted);
  assert(midiDispatch(a) == SendResult::Failed);
  fake::midi.clear(); fake::writable = true; settle();
  assert(fake::midi.size() == 48);
  std::cout << "PASS MIDI ordered retry / reconnect reset / overflow panic\n";
}
void keyboardTests() {
  reset();
  Action a; a.protocol = Protocol::Keyboard; a.transport = Transport::Ble;
  a.usage = 104; a.modifiers = 1; keyboardDispatch(a);
  a.usage = 105; keyboardDispatch(a);
  a.usage = 104; a.keyMessage = KeyMessage::Up;
  fake::writable = false; keyboardDispatch(a);
  fake::writable = true; settle();
  assert(fake::hid.back().keys[0] == 105 && fake::hid.back().modifiers == 1);
  a.usage = 105; keyboardDispatch(a); settle();
  assert(fake::hid.back().keys[0] == 0 && fake::hid.back().modifiers == 0);
  a.keyMessage = KeyMessage::Down;
  for (int key = 4; key < 10; ++key) { a.usage = key; assert(keyboardDispatch(a) == SendResult::Accepted); }
  a.usage = 10; assert(keyboardDispatch(a) == SendResult::Busy);
  ++fake::epoch[1]; settle();
  assert(fake::hid.back().keys[0] == 0);
  // A rapid disconnect/re-subscribe must clear reports even if readiness
  // returns true before the loop sees the disconnected state.
  a.usage = 104; keyboardDispatch(a); ++fake::epoch[1]; settle();
  assert(fake::hid.back().keys[0] == 0);
  std::cout << "PASS HID held-key union / retry / 6KRO / epoch reset\n";
}
void midiBothTests() {
  reset();
  Action a; a.protocol = Protocol::Midi; a.transport = Transport::Both;
  assert(midiDispatch(a) == SendResult::Accepted);
  a.message = MidiMessage::NoteOff; a.value = 0;
  assert(midiDispatch(a) == SendResult::Accepted);
  settle();
  assert(fake::midi.size() == 4);
  assert(fake::midi[0].transport == Transport::Usb && fake::midi[1].transport == Transport::Ble);
  assert(fake::midi[2].status == 0x80 && fake::midi[3].status == 0x80);
  for (unsigned offline = 0; offline < 2; ++offline) {
    reset(); fake::connected[offline] = false; settle(); fake::midi.clear();
    a.message = MidiMessage::CC; a.number = 10; a.value = 64;
    assert(midiDispatch(a) == SendResult::Accepted);
    assert(fake::midi.size() == 1 && fake::midi[0].transport == transportAt(1 - offline));
  }
  fake::connected[0] = fake::connected[1] = false; settle();
  assert(midiDispatch(a) == SendResult::Unavailable);
  // Saturating USB must not prevent BLE delivery or duplicate it on retry.
  reset(); fake::writable = false;
  Action usb = a; usb.transport = Transport::Usb;
  for (int i = 0; i < 64; ++i) midiDispatch(usb);
  assert(midiDispatch(a) == SendResult::Accepted);
  fake::writable = true; settle();
  unsigned bleCount = 0;
  for (const auto& m : fake::midi) if (m.transport == Transport::Ble) ++bleCount;
  assert(bleCount == 1);
  std::cout << "PASS MIDI Both fanout / single-side availability / isolated overflow\n";
}
void engineTests() {
  reset(); config.chains[0].count = config.chains[1].count = 3;
  auto& p = config.chains[0].actions; auto& r = config.chains[1].actions;
  p[0] = Action{}; p[1] = Action{}; p[1].protocol = Protocol::Midi; p[1].transport = Transport::Usb;
  p[2] = Action{}; p[2].protocol = Protocol::Keyboard; p[2].transport = Transport::Ble;
  for (int i = 0; i < 3; ++i) r[i] = p[i];
  r[0].intValue = 0; r[1].message = MidiMessage::NoteOff; r[1].value = 0; r[2].keyMessage = KeyMessage::Up;
  engine.trigger({0, 0}); engine.trigger({1, 1}); settle();
  assert(engine.stats.accepted == 6 && fake::midi.size() == 2);
  assert(fake::hid.front().keys[0] == 104 && fake::hid.back().keys[0] == 0);
  fake::wifi = false; engine.trigger({0, 2}); settle();
  assert(engine.stats.skipped == 1 && engine.stats.accepted == 8);
  assert(!engine.trigger({28, 3}));
  std::cout << "PASS heterogeneous Press/Release chain / unavailable protocol isolation\n";
}
int main() {
  configTests(); oscTests(); midiTests(); keyboardTests(); midiBothTests(); engineTests();
  std::cout << "All firmware host tests passed.\n";
}
