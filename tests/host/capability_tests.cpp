#include "model.h"
#include "save_helper.h"
#include "engine.h"
#include "backend_internal.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace chimera;
static_assert(!HAS_USB_MIDI && !HAS_USB_KEYBOARD, "C-series must be BLE-only");
namespace {
std::vector<uint8_t> midiStatuses;
std::vector<KeyboardReport> reports;
}
namespace chimera {
void transportsBegin() {}
void transportsTick() {}
bool midiReady(Transport t) { return t == Transport::Ble; }
bool keyboardReady(Transport t) { return t == Transport::Ble; }
uint32_t midiEpoch(Transport) { return 0; }
uint32_t keyboardEpoch(Transport) { return 0; }
bool midiWrite(Transport t, uint8_t status, uint8_t, uint8_t) {
  assert(t == Transport::Ble); midiStatuses.push_back(status); return true;
}
bool keyboardWrite(Transport t, const KeyboardReport& report) {
  assert(t == Transport::Ble); reports.push_back(report); return true;
}
SendResult oscDispatch(const Action&) { return SendResult::Accepted; }
}
int main() {
  JsonDocument caps;
  encodeCapabilities(caps);
  assert(caps["usbMidi"] == false && caps["usbKeyboard"] == false);
  assert(caps["defaultMidiTransport"] == "ble");
  assert(caps["midiTransports"].size() == 1 && caps["midiTransports"][0] == "ble");
  assert(caps["keyboardTransports"].size() == 1 && caps["keyboardTransports"][0] == "ble");
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  assert(String(HARDWARE_NAME) == "XIAO ESP32C3 / ChainOSCPad PCB");
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  assert(String(HARDWARE_NAME) == "XIAO ESP32C6 / ChainOSCPad PCB");
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
  assert(String(HARDWARE_NAME) == "XIAO ESP32C5 / ChainOSCPad PCB");
#endif
  Config source, decoded;
  source.encoderRotation.outputCount = 1;
  source.encoderRotation.outputs[0].kind = RotationOutputKind::MidiCC;
  source.encoderRotation.outputs[0].transport = Transport::Both;
  source.chains[0].count = source.chains[1].count = 3;
  Action midi; midi.protocol = Protocol::Midi; midi.transport = Transport::Both;
  Action hid; hid.protocol = Protocol::Keyboard; hid.transport = Transport::Usb;
  source.chains[0].actions[0] = midi;
  source.chains[0].actions[1] = hid;
  source.chains[0].actions[2] = Action{};
  midi.message = MidiMessage::NoteOff; midi.value = 0;
  hid.keyMessage = KeyMessage::Up;
  source.chains[1].actions[0] = midi;
  source.chains[1].actions[1] = hid;
  source.chains[1].actions[2] = Action{};
  JsonDocument doc; String error;
  encodeConfig(source, doc);
  assert(decodeConfig(doc.as<JsonVariantConst>(), decoded, error));
  assert(decoded.encoderRotation.outputs[0].transport == Transport::Ble);
  for (int i = 0; i < 2; ++i) {
    assert(decoded.chains[i].actions[0].transport == Transport::Ble);
    assert(decoded.chains[i].actions[1].transport == Transport::Ble);
    assert(decoded.chains[i].actions[2].transport == Transport::Wifi);
  }
  for (const char* transport : {"usb", "ble", "both"}) {
    doc["chains"][0]["actions"][0]["transport"] = transport;
    assert(decodeConfig(doc.as<JsonVariantConst>(), decoded, error));
    assert(decoded.chains[0].actions[0].transport == Transport::Ble);
  }
  doc["chains"][0]["actions"][0].remove("transport");
  assert(decodeConfig(doc.as<JsonVariantConst>(), decoded, error));
  assert(decoded.chains[0].actions[0].transport == Transport::Ble);
  doc["chains"][0]["actions"][0]["transport"] = "invalid";
  assert(!decodeConfig(doc.as<JsonVariantConst>(), decoded, error));
  encodeConfig(source, doc);
  doc["chains"][0]["actions"][1]["transport"] = "both";
  assert(!decodeConfig(doc.as<JsonVariantConst>(), decoded, error));

  encodeConfig(source, doc);
  String json; serializeJson(doc, json);
  assert(saveSource(source, error));
  assert(loadConfig(error));
  assert(config.encoderRotation.outputCount == 1 && config.encoderRotation.outputs[0].transport == Transport::Ble);
  encodeConfig(config, doc);
  assert(doc["chains"][0]["actions"][0]["transport"] == "ble");
  assert(doc["chains"][0]["actions"][1]["transport"] == "ble");
  for (int i = 0; i < 10; ++i) backendsTick(0);
  midiStatuses.clear(); reports.clear();
  engine.trigger({0, 0}); engine.trigger({1, 1});
  for (int i = 0; i < 10; ++i) backendsTick(0);
  assert(engine.stats.accepted == 6 && engine.stats.skipped == 0);
  assert((midiStatuses == std::vector<uint8_t>{0x90, 0x80}));
  assert(reports.size() == 2 && reports[0].keys[0] == 104 && reports[1].keys[0] == 0);
  std::cout << "PASS " << HARDWARE_NAME << ": capabilities / BLE normalization / save-load / press-release dispatch\n";
}
