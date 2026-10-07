#include "model.h"
#include "rotation_runtime.h"
#include "rotation_sender.h"
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
bool rejectChainAllocation = false;
int rotationAllocationsUntilFailure = -1;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (rejectConfigAllocation && size >= sizeof(Config)) return nullptr;
  if (rejectChainAllocation && size == sizeof(Chain)) return nullptr;
  if (size == sizeof(EncoderRotationSettings)) {
    if (rotationAllocationsUntilFailure == 0) return nullptr;
    if (rotationAllocationsUntilFailure > 0) --rotationAllocationsUntilFailure;
  }
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
    chain.count = 8;
    for (unsigned i = 0; i < chain.count; ++i) chain.actions[i] = on;
  }
  assert(encoded(source).length() > 4096);
  rejectConfigAllocation = true;
  assert(saveSource(source, error));
  rejectConfigAllocation = false;
  config.chains[27].count = 0;
  assert(loadConfig(error)); assert(config.chains[27].count == 8);
  assert(!beginConfigSave(String(std::string(MAX_RECORD_BYTES + 1, ' ')), error));
  assert(config.chains[27].count == 8);
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
  memset(a.address, 'x', MAX_OSC_ADDRESS_BYTES); a.address[0]='/'; a.address[MAX_OSC_ADDRESS_BYTES]=0;
  memset(a.stringValue, 'y', MAX_OSC_STRING_BYTES); a.stringValue[MAX_OSC_STRING_BYTES]=0;
  assert(oscDispatch(a)==SendResult::Accepted);
  assert(fake::osc.size()==332 && fake::osc[191]=='x' && fake::osc[192]==0);
  assert(fake::osc[196]==',' && fake::osc[197]=='s' && fake::osc[200]=='y' && fake::osc[327]=='y' && fake::osc[331]==0);
  Chain chain, decoded; chain.count=1;chain.actions[0]=a;JsonDocument doc;String error;
  encodeChain(chain,0,doc);assert(decodeChain(doc.as<JsonVariantConst>(),0,decoded,error));
  doc["actions"][0]["address"]=std::string("/")+std::string(192,'x');assert(!decodeChain(doc.as<JsonVariantConst>(),0,decoded,error));
  encodeChain(chain,0,doc);doc["actions"][0]["value"]=std::string(129,'y');assert(!decodeChain(doc.as<JsonVariantConst>(),0,decoded,error));
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
  for (int i = 0; i < MAX_TOTAL_ACTIONS + 64; ++i) assert(midiDispatch(a) == SendResult::Accepted);
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
  for (int i = 0; i < MAX_TOTAL_ACTIONS + 64; ++i) midiDispatch(usb);
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
  JsonDocument request; request["schemaVersion"]=2; request["network"]=doc;
  String network; serializeJson(request, network);
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
  encodeRotation(source.encoderRotation, doc); chain=""; serializeJson(doc,chain);
  assert(stageConfigRotation(token, chain, error));
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
  memset(action.address, '"', MAX_OSC_ADDRESS_BYTES); action.address[0] = '/'; action.address[MAX_OSC_ADDRESS_BYTES] = 0;
  memset(action.stringValue, 1, MAX_OSC_STRING_BYTES); action.stringValue[MAX_OSC_STRING_BYTES] = 0;
  for (uint8_t id=0;id<INPUT_COUNT;++id) {
    auto& chain=source.chains[id];
    chain.count = id>=26?8:id%2?0:16;
    for (unsigned i = 0; i < chain.count; ++i) chain.actions[i] = action;
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
  assert(config.chains[0].count == 16 && config.chains[0].actions[15].stringValue[127] == 1);
  assert(config.chains[27].count == 8 && config.chains[27].actions[7].stringValue[127] == 1);
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
void allNotesTests() {
  reset(); config = Config{};
  Action note; note.protocol=Protocol::Midi; note.transport=Transport::Both; note.number=60;
  config.chains[0].count=2; config.chains[0].actions[0]=note; config.chains[0].actions[1]=note;
  note.message=MidiMessage::NoteOff; note.transport=Transport::Ble;
  config.chains[1].count=1; config.chains[1].actions[0]=note;
  note.channel=2; note.transport=Transport::Usb;
  config.chains[27].count=1; config.chains[27].actions[0]=note;
  Action bulk; bulk.protocol=Protocol::Midi; bulk.message=MidiMessage::AllNotesOn; bulk.value=73;
  config.chains[2].count=1; config.chains[2].actions[0]=bulk;
  note.message=MidiMessage::CC; note.number=9;
  config.chains[3].count=1; config.chains[3].actions[0]=note;
  assert(midiDispatch(bulk)==SendResult::Accepted); settle();
  assert(fake::midi.size()==3);
  for (const auto& m : fake::midi) assert(m.a==60 && m.b==73 && (m.status&0xf0)==0x90);
  assert(fake::midi[0].transport==Transport::Usb && fake::midi[1].status==0x91 && fake::midi[2].transport==Transport::Ble);
  String error; Config source=config; assert(saveSource(source,error)); assert(loadConfig(error));
  assert(config.chains[2].actions[0].message==MidiMessage::AllNotesOn);
  JsonDocument doc; encodeChain(config.chains[2],2,doc); Chain decoded;
  doc["actions"][0]["value"]=0; assert(!decodeChain(doc.as<JsonVariantConst>(),2,decoded,error));
  bulk.message=MidiMessage::AllNotesOff;
  reset(); fake::connected[0]=false;
  assert(midiDispatch(bulk)==SendResult::Accepted); settle();
  assert(fake::midi.size()==1 && fake::midi[0].status==0x80 && fake::midi[0].b==0);
  // Maximum-sized target set under backpressure, followed by an ordinary Action.
  reset(); config=Config{}; unsigned n=0;
  for(auto& c:config.chains){c.count=8; for(unsigned k=0;k<c.count;++k){auto& a=c.actions[k];a.protocol=Protocol::Midi;a.transport=Transport::Usb;a.channel=n/128+1;a.number=n%128;++n;}}
  fake::writable=false; assert(midiDispatch(bulk)==SendResult::Accepted);
  assert(midiDispatch(bulk)==SendResult::Failed); // no partial second batch
  note.message=MidiMessage::NoteOn;note.number=127;note.channel=16;note.value=99;
  assert(midiDispatch(note)==SendResult::Accepted);
  fake::writable=true; for(int i=0;i<40;++i)backendsTick(0);
  assert(fake::midi.size()==225);
  for(unsigned i=0;i<224;++i)assert(fake::midi[i].status==(0x80|i/128)&&fake::midi[i].a==i%128&&fake::midi[i].b==0);
  assert(fake::midi.back().status==0x9f && fake::midi.back().b==99);
  // Normal Chain composition with Wait, without collecting All Notes itself.
  reset(); config=Config{}; config.chains[0].count=1;config.chains[0].actions[0]=note;
  auto& c=config.chains[1];c.count=3;c.actions[0]=bulk;c.actions[0].message=MidiMessage::AllNotesOn;c.actions[0].value=80;
  c.actions[1].protocol=Protocol::Wait;c.actions[1].delayMs=1000;c.actions[2]=bulk;
  Engine scheduler;hostMillis=100;scheduler.trigger({1,100});assert(fake::midi.size()==1&&fake::midi[0].b==80);
  scheduler.tick(1099);assert(fake::midi.size()==1);scheduler.tick(1100);assert(fake::midi.size()==2&&fake::midi.back().status==0x8f);
  hostMillis=1234;config=Config{};fake::midi.clear();assert(midiDispatch(bulk)==SendResult::Accepted);assert(fake::midi.empty());
  std::cout << "PASS All Notes dedup / routing / explicit messages / maximum batch retry-order / Wait / persistence / empty targets\n";
}
void sharedSlotsTests() {
  String error; Config source, decoded;
  static_assert(sizeof(Config) < sizeof(Action) * MAX_TOTAL_ACTIONS + sizeof(EncoderRotationSettings) + 1024, "Config must own only 224 Action slots plus rotation settings");
  for(unsigned key=0;key<13;++key)for(unsigned i=0;i<16;++i)
    assert(&source.chains[key*2].actions[i]==&source.chains[key*2+1].actions[15-i]);
  assert(&source.chains[26].actions[7]+1==&source.chains[27].actions[0]);
  Action note; note.protocol=Protocol::Midi; note.transport=Transport::Usb;
  const unsigned counts[][2]={{16,0},{14,2},{8,8},{1,15},{0,16}};
  for(const auto& pair:counts){
    source.chains[0].count=pair[0];source.chains[1].count=pair[1];
    for(unsigned event=0;event<2;++event)for(unsigned i=0;i<pair[event];++i){note.number=i;source.chains[event].actions[i]=note;}
    JsonDocument doc;encodeConfig(source,doc);assert(decodeConfig(doc.as<JsonVariantConst>(),decoded,error));
    Config copy=source;assert(encoded(copy)==encoded(source));
    decoded=copy;copy.chains[pair[0]?0:1].actions[0].number=100;
    assert(encoded(decoded)==encoded(source)); // Copies must rebind views to their own pools.
    auto array=doc["chains"].as<JsonArray>();JsonDocument reverse;encodeNetwork(source,reverse);
    JsonDocument reordered;reordered["schemaVersion"]=2;reordered["network"]=reverse;
    reordered["encoderRotation"]=doc["encoderRotation"];
    auto reversed=reordered["chains"].to<JsonArray>();for(int i=27;i>=0;--i)reversed.add(array[i]);
    assert(decodeConfig(reordered.as<JsonVariantConst>(),decoded,error)&&encoded(decoded)==encoded(source));
    assert(saveSource(source,error));assert(loadConfig(error));
    assert(config.chains[0].count==pair[0]&&config.chains[1].count==pair[1]);
    reset();Engine e;assert(e.trigger({0,0}));assert(e.trigger({1,0}));settle();assert(fake::midi.size()==16);
    for(unsigned i=0;i<pair[0];++i)assert(fake::midi[i].a==i);
    for(unsigned i=0;i<pair[1];++i)assert(fake::midi[pair[0]+i].a==i);
  }
  const String previous=encoded(config);
  const auto savedDisk=*fake::files.at("/config.records");
  rejectChainAllocation=true;
  assert(!saveSource(source,error));assert(encoded(config)==previous);
  assert(*fake::files.at("/config.records")==savedDisk);
  rejectChainAllocation=false;
  source.chains[0].count=10;source.chains[1].count=7;
  JsonDocument doc;encodeConfig(source,doc);assert(!decodeConfig(doc.as<JsonVariantConst>(),decoded,error));
  assert(!saveSource(source,error));assert(encoded(config)==previous);assert(loadConfig(error)&&encoded(config)==previous);
  // Storage validation must also reject an over-budget snapshot independently of staging.
  JsonDocument header;encodeNetwork(source,doc);header["storageVersion"]=2;header["network"]=doc;
  String corrupt;serializeJson(header,corrupt);corrupt.concat("\n");
  for(uint8_t i=0;i<INPUT_COUNT;++i){encodeChain(source.chains[i],i,doc);serializeJson(doc,corrupt);corrupt.concat("\n");}
  const auto original=*fake::files.at("/config.records");*fake::files.at("/config.records")=corrupt.c_str();
  assert(!loadConfig(error));for(const auto& c:config.chains)assert(c.count==0);
  *fake::files.at("/config.records")=original;assert(loadConfig(error));
  source.chains[0].count=source.chains[1].count=0;source.chains[24].count=16;source.chains[25].count=0;
  assert(saveSource(source,error));source.chains[25].count=1;assert(!saveSource(source,error));
  source.chains[25].count=0;source.chains[26].count=9;assert(!saveSource(source,error));
  std::cout<<"PASS shared 16-slot physical pool / independent copies / shuffled Events / order / save-load / allocation failure / encoder limits\n";
  std::cout<<"Resource sizes: Action="<<sizeof(Action)<<" Chain="<<sizeof(Chain)<<" Config="<<sizeof(Config)<<"\n";
}
void rotationTests();
void rotationStorageTests();
void rotationRuntimeTests() {
  Config source; String error; Engine e;
  auto& r=source.encoderRotation;
  r.mode=RotationMode::RotationValue;r.axis={100,73,RotationBoundary::Stop};
  assert(saveSource(source,error));assert(rotationRuntime.active()&&rotationRuntime.position()==73);
  reset();assert(e.trigger({26,0})&&rotationRuntime.position()==74);
  assert(e.trigger({27,0})&&rotationRuntime.position()==73);
  assert(fake::midi.empty()&&fake::hid.empty());
  // Apply each independent edit while retaining Position, even with no Outputs.
  source.chains[0].count=1;source.chains[0].actions[0].protocol=Protocol::Wait;
  assert(saveSource(source,error)&&rotationRuntime.position()==73);
  source.chains[24].count=1;source.chains[24].actions[0].protocol=Protocol::Wait;
  assert(saveSource(source,error)&&rotationRuntime.position()==73);
  r.outputCount=1;r.outputs[0].kind=RotationOutputKind::MidiCC;r.outputs[0].transport=Transport::Both;
  assert(saveSource(source,error)&&rotationRuntime.position()==73);
  r.axis.initialPosition=50;assert(saveSource(source,error)&&rotationRuntime.position()==73);
  r.axis.boundary=RotationBoundary::Wrap;assert(saveSource(source,error)&&rotationRuntime.position()==73);
  r.axis.rangeSteps=0;assert(!saveSource(source,error)&&rotationRuntime.position()==73);
  r.axis.rangeSteps=100;
  e.cancelAll();backendsPanic();assert(rotationRuntime.position()==73);
  ++fake::epoch[0];++fake::epoch[1];settle();assert(rotationRuntime.position()==73);
  assert(fake::midi.empty());
  r.axis.rangeSteps=65535;r.axis.initialPosition=65535;
  assert(saveSource(source,error)&&rotationRuntime.position()==65535);
  assert(e.trigger({26,0})&&rotationRuntime.position()==0);
  assert(e.trigger({27,0})&&rotationRuntime.position()==65535);
  r.axis.boundary=RotationBoundary::Stop;assert(saveSource(source,error));
  assert(e.trigger({26,0})&&rotationRuntime.position()==65535);
  r.axis.rangeSteps=1;r.axis.initialPosition=0;
  assert(saveSource(source,error)&&rotationRuntime.position()==0);
  assert(e.trigger({27,0})&&rotationRuntime.position()==0);
  assert(e.trigger({26,0})&&rotationRuntime.position()==1);
  assert(e.trigger({26,0})&&rotationRuntime.position()==1);
  r.axis.boundary=RotationBoundary::Wrap;assert(saveSource(source,error));
  assert(e.trigger({26,0})&&rotationRuntime.position()==0);
  assert(e.trigger({27,0})&&rotationRuntime.position()==1);
  assert(loadConfig(error)&&rotationRuntime.position()==0);
  r.mode=RotationMode::ActionChain;assert(saveSource(source,error)&&!rotationRuntime.active());
  r.axis.initialPosition=1;r.mode=RotationMode::RotationValue;
  assert(saveSource(source,error)&&rotationRuntime.position()==1);
  assert(fake::midi.empty());
  // All Notes sees Key and Push, but excludes inactive rotation chains.
  for(uint8_t id : {uint8_t(0),uint8_t(24),uint8_t(26),uint8_t(27)}){
    source.chains[id].count=1;auto& a=source.chains[id].actions[0];a=Action{};
    a.protocol=Protocol::Midi;a.transport=Transport::Usb;a.number=40+id;
  }
  assert(saveSource(source,error));reset();
  Action bulk;bulk.protocol=Protocol::Midi;bulk.message=MidiMessage::AllNotesOff;
  midiDispatch(bulk);settle();assert(fake::midi.size()==2);
  assert(fake::midi[0].a==40&&fake::midi[1].a==64);
  reset();assert(e.trigger({26,0}));settle();assert(fake::midi.empty());
  r.mode=RotationMode::ActionChain;assert(saveSource(source,error));reset();
  assert(e.trigger({26,0})&&e.trigger({27,0}));settle();assert(fake::midi.size()==2);
  reset();midiDispatch(bulk);settle();assert(fake::midi.size()==4);
  std::cout<<"PASS Rotation runtime / mode routing / apply-retain-reset / Stop Wrap 1+65535 / zero outputs / no Rotation sends / active-only All Notes; Runtime="<<sizeof(RotationRuntime)<<"\n";
}
void rotationSenderTests() {
  Config source; String error; Engine e;
  auto& r = source.encoderRotation;
  r.mode = RotationMode::RotationValue; r.axis = {100, 0, RotationBoundary::Stop};
  r.outputCount = 3;
  r.outputs[0].range.integer = {-100, 100};
  r.outputs[1].kind = RotationOutputKind::OscFloat; r.outputs[1].range.floating = {1, -1};
  r.outputs[2].kind = RotationOutputKind::MidiCC; r.outputs[2].transport = Transport::Both;
  r.outputs[2].range.integer = {0, 1}; r.outputs[2].channel = 16; r.outputs[2].number = 127;
  assert(saveSource(source, error)); reset(); fake::wifi=true; fake::packets.clear();
  rotationSendStats = {};
  assert(e.trigger({26,0})); assert(rotationSendStats.generation == 1 && rotationPendingCount() == 4);
  assert(e.trigger({26,0})); assert(rotationSendStats.overwritten == 4 && rotationSendStats.snapshot == 2);
  assert(fake::midi.empty() && fake::packets.empty()); // Publish never writes.
  rotationSendTick();
  assert(rotationPendingCount() == 0 && fake::packets.size() == 2 && fake::midi.size() == 2);
  const auto& integerPacket = fake::packets[0];
  assert(integerPacket[integerPacket.size()-1] == 160); // -96 as big-endian int32.
  const auto& floatPacket = fake::packets[1]; uint32_t bits=0;
  for (size_t i=floatPacket.size()-4;i<floatPacket.size();++i) bits=(bits<<8)|floatPacket[i];
  float f; memcpy(&f,&bits,4); assert(f == float(.96));
  for (const auto& m : fake::midi) assert(m.status == 0xbf && m.a == 127 && m.b == 0);
  assert(e.trigger({26,0})); rotationSendTick(); assert(fake::midi.size() == 4); // Same rounded CC, new generation.
  // Backlog overwrites, ordinary MIDI FIFO has strict priority; OSC still progresses.
  fake::writable=false;
  Action note; note.protocol=Protocol::Midi; note.transport=Transport::Both;
  assert(midiDispatch(note)==SendResult::Accepted);
  for(int i=0;i<20;++i) assert(e.trigger({26,0}));
  rotationSendTick(); assert(rotationPendingCount()==2);
  fake::writable=true; midiTick();
  const auto before=fake::midi.size(); assert(before==6);
  rotationSendTick(); assert(fake::midi.size()==before+2 && rotationPendingCount()==0);
  // Failed API writes discarded once, never retried; one route failure does not stop others.
  fake::udpWritable=false; fake::writable=false;
  e.trigger({26,0}); rotationSendTick();
  assert(rotationPendingCount()==0 && rotationSendStats.failed[0]==2 && rotationSendStats.failed[1]==1 && rotationSendStats.failed[2]==1);
  fake::udpWritable=true; fake::writable=true; auto count=fake::midi.size(); rotationSendTick(); assert(fake::midi.size()==count);
  // Disconnect and brief reconnect epochs discard per transport independently.
  e.trigger({26,0}); ++fake::epoch[1]; rotationSendTick();
  assert(rotationPendingCount()==0 && fake::midi.size()==count+1); // USB only.
  count=fake::midi.size(); rotationSendTick(); assert(fake::midi.size()==count);
  e.trigger({26,0}); rotationWifiChanged(); auto packets=fake::packets.size(); rotationSendTick(); assert(fake::packets.size()==packets);
  fake::connected[0]=fake::connected[1]=false; fake::wifi=false;
  e.trigger({26,0}); assert(rotationPendingCount()==0);
  fake::connected[0]=fake::connected[1]=true; fake::wifi=true;
  count=fake::midi.size(); rotationSendTick(); assert(fake::midi.size()==count);
  // Failed save retains pending; successful save/panic discards without regenerating.
  e.trigger({26,0}); auto position=rotationRuntime.position();
  r.axis.rangeSteps=0; assert(!saveSource(source,error) && rotationPendingCount()==4);
  r.axis.rangeSteps=100; assert(saveSource(source,error) && rotationPendingCount()==0 && rotationRuntime.position()==position);
  e.trigger({26,0}); position=rotationRuntime.position(); backendsPanic();
  assert(rotationPendingCount()==0 && rotationRuntime.position()==position);
  // Stop unchanged leaves existing pending intact. Wrap creates another generation.
  r.axis={1,0,RotationBoundary::Stop}; assert(saveSource(source,error));
  e.trigger({26,0}); const auto generation=rotationSendStats.generation;
  e.trigger({26,0}); assert(rotationSendStats.generation==generation && rotationPendingCount()==4);
  r.axis.boundary=RotationBoundary::Wrap; assert(saveSource(source,error));
  e.trigger({26,0}); assert(rotationRuntime.position()==0 && rotationSendStats.generation==generation+1);
  // All 16 duplicate destinations stay independent, round-robin drains in bounded ticks.
  r.outputCount=16; for(auto& output:r.outputs) { output=RotationOutput{}; output.kind=RotationOutputKind::MidiCC; output.transport=Transport::Both; }
  assert(saveSource(source,error)); reset(); e.trigger({26,0}); assert(rotationPendingCount()==32);
  for(unsigned tick=0;tick<8;++tick) {
    const auto size=fake::midi.size(); rotationSendTick(); assert(fake::midi.size()==size+ROTATION_SEND_BUDGET);
  }
  assert(rotationPendingCount()==0 && fake::midi.size()==32);
  // Sustained generation replacement still services every Output, while ordinary
  // Note Actions dispatch first and never share Rotation storage.
  for(unsigned i=0;i<16;++i) r.outputs[i].number=uint8_t(i);
  assert(saveSource(source,error)); reset();
  for(unsigned tick=0;tick<16;++tick) {
    e.trigger({26,0}); const auto size=fake::midi.size();
    assert(midiDispatch(note)==SendResult::Accepted);
    assert(fake::midi.size()==size+2 && (fake::midi[size].status&0xf0)==0x90);
    rotationSendTick(); assert(fake::midi.size()==size+6);
  }
  bool seen[2][16]{};
  for(const auto& m:fake::midi) if((m.status&0xf0)==0xb0) seen[transportIndex(m.transport)][m.a]=true;
  for(const auto& route:seen) for(bool value:route) assert(value);
  // A Wi-Fi API failure cannot stop BLE; generation zero remains a valid slot.
  r.outputCount=2; r.outputs[0]=RotationOutput{}; r.outputs[1]=r.outputs[15];
  assert(saveSource(source,error)); reset(); fake::udpWritable=false;
  rotationSendStats.generation=UINT32_MAX; e.trigger({26,0});
  assert(rotationSendStats.generation==0 && rotationPendingCount()==3);
  rotationSendTick(); assert(fake::midi.size()==2 && rotationPendingCount()==0);
  fake::udpWritable=true;
  r.outputCount=0; assert(saveSource(source,error)); auto g=rotationSendStats.generation;
  e.trigger({26,0}); assert(rotationSendStats.generation==g+1 && rotationPendingCount()==0);
  std::cout<<"PASS Rotation Latest-State snapshots / overwrite / equal values / API discard / no reconnect replay / lifecycle / FIFO priority / budget fairness / duplicates; pending="<<rotationPendingBytes()<<" sender="<<rotationSenderBytes()<<"\n";
}
int main() {
  rotationTests();
  configTests(); wifiSaveTests(); transactionTests(); settingsScaleTests(); oscTests(); midiTests(); keyboardTests(); midiBothTests(); engineTests(); waitTests(); allNotesTests(); sharedSlotsTests();
  rotationStorageTests();
  rotationRuntimeTests();
  rotationSenderTests();
  std::cout << "All firmware host tests passed.\n";
}
