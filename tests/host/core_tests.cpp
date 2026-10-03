#include "model.h"
#include "engine.h"
#include "backend_internal.h"
#include "LittleFS.h"
#include "save_helper.h"
#include "WiFiUdp.h"
#include "WiFi.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <new>

using namespace chimera;
bool rejectConfigAllocation = false;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (rejectConfigAllocation && size >= sizeof(Config)) return nullptr;
  try { return ::operator new(size); } catch (...) { return nullptr; }
}
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
  assert(saveSource(source, error));
  fake::writeFailure = true; source.oscPort = 7777;
  assert(!saveSource(source, error)); assert(config.oscPort == 9000);
  fake::writeFailure = false; config.chains[0].count = 0;
  assert(loadConfig(error)); assert(config.chains[0].count == 1);
  // Exercise every available Action slot using bounded records.
  for (auto& chain : source.chains) {
    chain.count = MAX_ACTIONS;
    for (auto& action : chain.actions) action = on;
  }
  assert(encoded(source).length() > 4096);
  rejectConfigAllocation = true;
  assert(saveSource(source, error));
  rejectConfigAllocation = false;
  config.chains[27].count = 0;
  assert(loadConfig(error)); assert(config.chains[27].count == MAX_ACTIONS);
  assert(!beginConfigSave(String(std::string(MAX_RECORD_BYTES + 1, ' ')), error));
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
  assert(fake::midi.empty());
  // Overflow drops the backlog without synthesizing MIDI messages.
  fake::writable = false;
  for (int i = 0; i < 64; ++i) assert(midiDispatch(a) == SendResult::Accepted);
  assert(midiDispatch(a) == SendResult::Failed);
  fake::midi.clear(); fake::writable = true; settle();
  assert(fake::midi.empty());
  fake::writable = false;
  assert(midiDispatch(a) == SendResult::Accepted);
  midiPanic(); fake::writable = true; settle();
  assert(fake::midi.empty());
  a.message = MidiMessage::CC; a.number = 123; a.value = 0;
  assert(midiDispatch(a) == SendResult::Accepted);
  assert(fake::midi.size() == 1 && fake::midi[0].status == 0xb0 && fake::midi[0].a == 123);
  std::cout << "PASS MIDI ordered retry / silent reconnect, overflow and cancellation / explicit CC\n";
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
void wifiSaveTests() {
  String error;
  const String before = encoded(config);
  rejectConfigAllocation = true;
  assert(saveWifiConfig("{\"ssid\":\"C6-test\",\"password\":\"test-password\"}", error));
  rejectConfigAllocation = false;
  JsonDocument expected;
  deserializeJson(expected, before);
  expected["network"]["ssid"] = "C6-test";
  expected["network"]["password"] = "test-password";
  String updated; serializeJson(expected, updated);
  assert(encoded(config) == updated);
  fake::writeFailure = true;
  assert(!saveWifiConfig("{\"ssid\":\"failed\",\"password\":\"\"}", error));
  assert(encoded(config) == updated);
  fake::writeFailure = false;
  assert(!saveWifiConfig("{\"ssid\":42,\"password\":\"\"}", error));
  assert(!saveWifiConfig("{\"ssid\":\"123456789012345678901234567890123\",\"password\":\"\"}", error));
  assert(encoded(config) == updated);
  config.ssid[0] = 0;
  assert(loadConfig(error) && encoded(config) == updated);
  std::cout << "PASS Wi-Fi save without Config allocation / preserve chains / failed write atomicity / reload\n";
}
void transactionTests() {
  String error; const String original = encoded(config);
  const std::string disk = *fake::files.at("/config.records");
  JsonDocument doc; encodeNetwork(config, doc); doc["oscPort"] = 9012;
  String network; serializeJson(doc, network);
  auto first = beginConfigSave(network, error);
  assert(first && !commitConfigSave(first, error));
  auto second = beginConfigSave(network, error);
  encodeChain(config.chains[0], 0, doc); String chain; serializeJson(doc, chain);
  assert(!stageConfigChain(first, 0, chain, error));
  assert(stageConfigChain(second, 0, chain, error));
  // Simulate restart after a partial transaction. Only the active snapshot loads.
  assert(loadConfig(error) && encoded(config) == original);
  assert(!commitConfigSave(second, error));
  Config source = config; source.oscPort = 9012;
  fake::writesUntilFailure = 8;
  assert(!saveSource(source, error));
  fake::writesUntilFailure = -1;
  assert(encoded(config) == original && *fake::files.at("/config.records") == disk);
  fake::renameFailure = true;
  assert(!saveSource(source, error));
  fake::renameFailure = false;
  assert(loadConfig(error) && encoded(config) == original);
  // A fully staged but corrupt file is rejected before rename.
  auto token = beginConfigSave(network, error);
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    encodeChain(source.chains[i], i, doc); chain = ""; serializeJson(doc, chain);
    assert(stageConfigChain(token, i, chain, error));
  }
  fake::files.at("/pending.records")->pop_back();
  assert(!commitConfigSave(token, error));
  assert(encoded(config) == original && *fake::files.at("/config.records") == disk);
  // Corrupt active storage must not expose a partially loaded configuration.
  fake::files.at("/config.records")->pop_back();
  assert(!loadConfig(error));
  for (const auto& c : config.chains) assert(c.count == 0);
  assert(config.oscPort == 9000);
  *fake::files.at("/config.records") = disk;
  assert(loadConfig(error) && encoded(config) == original);
  std::cout << "PASS LittleFS interrupted transaction / partial write / rename failure / corrupt records\n";
}
void settingsScaleTests() {
  Config source;
  Action action; action.oscType = OscType::String;
  memset(action.address, 'x', 96); action.address[0] = '/'; action.address[96] = 0;
  memset(action.stringValue, 1, 64); action.stringValue[64] = 0;
  for (auto& chain : source.chains) {
    chain.count = MAX_ACTIONS;
    for (auto& a : chain.actions) a = action;
  }
  String error;
  rejectConfigAllocation = true;
  assert(saveSource(source, error));
  assert(loadConfig(error));
  rejectConfigAllocation = false;
  assert(fake::files.at("/config.records")->size() > 48000);
  assert(encoded(config) == encoded(source));
  assert(saveWifiConfig("{\"ssid\":\"large-config\",\"password\":\"\"}", error));
  assert(loadConfig(error));
  assert(config.chains[27].count == MAX_ACTIONS && config.chains[27].actions[7].stringValue[63] == 1);
  std::cout << "PASS maximum 224 long OSC Actions / >48KB snapshot / bounded save-load / Wi-Fi preservation\n";
}
void waitTests() {
  reset(); Engine scheduler;
  Config source;
  Action wait; wait.protocol=Protocol::Wait; wait.delayMs=100;
  Action note; note.protocol=Protocol::Midi; note.transport=Transport::Usb;
  auto& press=source.chains[0]; press.count=3;
  press.actions[0]=note; press.actions[1]=wait; press.actions[2]=note; press.actions[2].number=50;
  source.chains[1].count=1; source.chains[1].actions[0]=note;
  source.chains[1].actions[0].message=MidiMessage::NoteOff;
  source.chains[1].actions[0].value=0;
  source.chains[2].count=1; source.chains[2].actions[0]=note; source.chains[2].actions[0].number=60;
  String error; assert(saveSource(source,error)); assert(loadConfig(error));
  assert(config.chains[0].actions[1].protocol==Protocol::Wait && config.chains[0].actions[1].delayMs==100);
  JsonDocument doc; encodeChain(config.chains[0],0,doc); Chain decoded;
  assert(!doc["actions"][1].containsKey("transport"));
  for (int bad : {-1,86400001}) { doc["actions"][1]["delayMs"]=bad; assert(!decodeChain(doc.as<JsonVariantConst>(),0,decoded,error)); }
  doc["actions"][1]["delayMs"]=1.5; assert(!decodeChain(doc.as<JsonVariantConst>(),0,decoded,error));
  hostMillis=1000; assert(scheduler.trigger({0,0}));
  assert(scheduler.activeCount()==1 && fake::midi.size()==1);
  hostMillis=1010; assert(scheduler.trigger({1,0})); assert(scheduler.trigger({2,0})); assert(scheduler.trigger({0,0}));
  assert(fake::midi.size()==4 && fake::midi[1].status==0x80 && fake::midi[2].a==60);
  scheduler.tick(1099); assert(fake::midi.size()==4);
  scheduler.tick(1100); assert(fake::midi.size()==5 && fake::midi.back().a==50 && scheduler.activeCount()==1);
  scheduler.tick(1110); assert(fake::midi.size()==6 && scheduler.activeCount()==0);
  // Consecutive Waits measure from each Action's actual execution time.
  config.chains[0].actions[0]=wait; config.chains[0].actions[1]=wait;
  hostMillis=2000; scheduler.trigger({0,0}); scheduler.tick(2150);
  auto count=fake::midi.size(); scheduler.tick(2249); assert(fake::midi.size()==count);
  scheduler.tick(2250); assert(fake::midi.size()==count+1);
  // millis wrap-around and zero-duration Wait.
  config.chains[0].actions[0].delayMs=0;
  hostMillis=0xfffffff0u; scheduler.trigger({0,0});
  count=fake::midi.size(); scheduler.tick(83); assert(fake::midi.size()==count);
  scheduler.tick(84); assert(fake::midi.size()==count+1);
  // A full wait pool must not block an immediate Release or partially run a rejected Chain.
  config.chains[0]=press; // source's Note On -> Wait -> Note On
  hostMillis=3000;
  for (unsigned i=0;i<Engine::MAX_RUNNING;++i) assert(scheduler.trigger({0,0}));
  count=fake::midi.size(); assert(!scheduler.trigger({0,0}));
  assert(fake::midi.size()==count && scheduler.stats.rejectedChains==1);
  assert(scheduler.trigger({1,0}) && fake::midi.back().status==0x80);
  scheduler.cancelAll(); count=fake::midi.size(); scheduler.tick(4000);
  assert(fake::midi.size()==count && scheduler.activeCount()==0 && scheduler.stats.cancelledChains==32);
  hostMillis=1234;
  std::cout << "PASS Wait independent Press/Release/retrigger / consecutive and zero Wait / wrap / overload / cancellation / persistence\n";
}
int main() {
  configTests(); wifiSaveTests(); transactionTests(); settingsScaleTests(); oscTests(); midiTests(); keyboardTests(); midiBothTests(); engineTests(); waitTests();
  std::cout << "All firmware host tests passed.\n";
}
