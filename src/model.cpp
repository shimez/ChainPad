#include "model.h"
#include <IPAddress.h>
#include <cmath>

namespace chimera {
Config config;
String inputName(uint8_t id) {
  if (id < 24) return "key" + String(id / 2 + 1) + (id % 2 ? ".release" : ".press");
  const char* names[] = {"encoder.press", "encoder.release", "encoder.cw", "encoder.ccw"};
  return id < INPUT_COUNT ? names[id - 24] : "invalid";
}
namespace {
bool text(JsonVariantConst v, char* dst, size_t capacity) {
  if (!v.is<const char*>()) return false;
  JsonString s = v.as<JsonString>();
  if (s.size() >= capacity || strlen(s.c_str()) != s.size()) return false;
  memcpy(dst, s.c_str(), s.size() + 1);
  return true;
}
bool integer(JsonVariantConst v, int64_t low, int64_t high) {
  return v.is<int64_t>() && v.as<int64_t>() >= low && v.as<int64_t>() <= high;
}
bool parseAction(JsonVariantConst j, Action& a) {
  if (!j.is<JsonObjectConst>()) return false;
  String p = j["protocol"] | "";
  if (p == "wait") {
    if (!integer(j["delayMs"], 0, MAX_WAIT_MS)) return false;
    a.protocol = Protocol::Wait; a.delayMs = j["delayMs"]; return true;
  }
  if (!integer(j["delayMs"], 0, 0)) return false;
  a.delayMs = 0;
  if (p == "midi" && (j["message"] == "allNotesOn" || j["message"] == "allNotesOff")) {
    a.protocol = Protocol::Midi;
    a.message = j["message"] == "allNotesOn" ? MidiMessage::AllNotesOn : MidiMessage::AllNotesOff;
    if (a.message == MidiMessage::AllNotesOn && !integer(j["value"], 1, 127)) return false;
    a.value = a.message == MidiMessage::AllNotesOn ? j["value"].as<uint8_t>() : 0;
    return true;
  }
  String t = j["transport"] | "";
  if (p == "midi" && j["transport"].isUnbound()) t = "both";
  if (p == "osc") {
    if (t != "wifi" || !text(j["address"], a.address, sizeof(a.address)) || a.address[0] != '/') return false;
    for (const char* c = a.address; *c; ++c) if (static_cast<uint8_t>(*c) <= 32) return false;
    a.protocol = Protocol::Osc;
    a.transport = Transport::Wifi;
    String type = j["type"] | "";
    if (type == "int" && integer(j["value"], INT32_MIN, INT32_MAX)) {
      a.oscType = OscType::Int; a.intValue = j["value"];
    } else if (type == "float" && j["value"].is<double>() && std::isfinite(j["value"].as<float>())) {
      a.oscType = OscType::Float; a.floatValue = j["value"];
    } else if (type == "bool" && j["value"].is<bool>()) {
      a.oscType = OscType::Bool; a.boolValue = j["value"];
    } else if (type == "string" && text(j["value"], a.stringValue, sizeof(a.stringValue))) {
      a.oscType = OscType::String;
    } else return false;
    return true;
  }
  if (t != "usb" && t != "ble" && !(p == "midi" && t == "both")) return false;
  a.transport = t == "usb" ? Transport::Usb : t == "ble" ? Transport::Ble : Transport::Both;
  if (p == "midi") {
    a.protocol = Protocol::Midi;
    String m = j["message"] | "";
    if (m != "noteOn" && m != "noteOff" && m != "cc") return false;
    a.message = m == "noteOn" ? MidiMessage::NoteOn : m == "noteOff" ? MidiMessage::NoteOff : MidiMessage::CC;
    if (!integer(j["channel"], 1, 16) || !integer(j["number"], 0, 127) ||
        !integer(j["value"], m == "noteOn" ? 1 : 0, 127)) return false;
    a.channel = j["channel"]; a.number = j["number"]; a.value = j["value"];
  } else if (p == "keyboard") {
    a.protocol = Protocol::Keyboard;
    String m = j["message"] | "";
    if (m != "keyDown" && m != "keyUp" && m != "releaseAll") return false;
    a.keyMessage = m == "keyDown" ? KeyMessage::Down : m == "keyUp" ? KeyMessage::Up : KeyMessage::ReleaseAll;
    if (!integer(j["usage"], 4, 115) || !integer(j["modifiers"], 0, 255)) return false;
    a.usage = j["usage"]; a.modifiers = j["modifiers"];
  } else return false;
  // Normalize saved/imported S3 configs for BLE-only hardware as well as
  // API submissions. UI restrictions alone must not select nonexistent USB.
  if (!CHAINPAD_HAS_USB) a.transport = Transport::Ble;
  return true;
}
}
bool decodeConfig(JsonVariantConst root, Config& out, String& error) {
  error = "Invalid schema/network settings";
  if (!root.is<JsonObjectConst>() || !integer(root["schemaVersion"], 1, 1)) return false;
  if (!decodeNetwork(root["network"], out, error)) return false;
  auto chains = root["chains"].as<JsonArrayConst>();
  error = "Expected exactly 28 uniquely identified input chains";
  if (chains.size() != INPUT_COUNT) return false;
  bool seen[INPUT_COUNT] = {};
  for (auto j : chains) {
    if (!integer(j["input"], 0, INPUT_COUNT - 1)) return false;
    uint8_t id = j["input"];
    if (seen[id]) return false;
    seen[id] = true;
    if (!decodeChain(j, id, out.chains[id], error)) return false;
  }
  for (uint8_t id = 0; id < 26; id += 2) {
    if (out.chains[id].count + out.chains[id + 1].count > MAX_KEY_ACTIONS) {
      error = "Press / Release total exceeds 16 Actions at " + inputName(id); return false;
    }
  }
  error = "";
  return true;
}
bool decodeNetwork(JsonVariantConst n, NetworkSettings& out, String& error) {
  error = "Invalid network settings";
  if (!text(n["ssid"], out.ssid, sizeof(out.ssid)) || !text(n["password"], out.password, sizeof(out.password)) ||
      !text(n["oscHost"], out.oscHost, sizeof(out.oscHost)) || !integer(n["oscPort"], 1, 65535)) return false;
  IPAddress ip;
  if (!ip.fromString(out.oscHost)) return false;
  out.oscPort = n["oscPort"];
  error = "";
  return true;
}
bool decodeChain(JsonVariantConst j, uint8_t id, Chain& out, String& error) {
    error = "Invalid chain ID";
    if (!integer(j["input"], id, id)) return false;
    auto actions = j["actions"].as<JsonArrayConst>();
    error = "Invalid action list at " + inputName(id);
    if (!j["actions"].is<JsonArrayConst>() || actions.size() > eventActionLimit(id)) return false;
    out.count = 0;
    for (auto action : actions) {
      Action a;
      error = "Invalid action " + String(out.count + 1) + " at " + inputName(id);
      if (!parseAction(action, a)) return false;
      out.actions[out.count++] = a;
    }
  error = "";
  return true;
}
void encodeCapabilities(JsonDocument& doc) {
  doc.clear();
  doc["hardware"] = HARDWARE_NAME;
  doc["usbMidi"] = HAS_USB_MIDI;
  doc["usbKeyboard"] = HAS_USB_KEYBOARD;
  doc["defaultMidiTransport"] = HAS_USB_MIDI ? "both" : "ble";
  doc["maxWaitMs"] = MAX_WAIT_MS;
  auto midi = doc["midiTransports"].to<JsonArray>();
  if (HAS_USB_MIDI) { midi.add("both"); midi.add("usb"); }
  midi.add("ble");
  auto keyboard = doc["keyboardTransports"].to<JsonArray>();
  if (HAS_USB_KEYBOARD) keyboard.add("usb");
  keyboard.add("ble");
}
void encodeConfig(const Config& source, JsonDocument& doc) {
  doc.clear(); doc["schemaVersion"] = 1;
  JsonDocument part; encodeNetwork(source, part); doc["network"] = part;
  auto chains = doc["chains"].to<JsonArray>();
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    encodeChain(source.chains[i], i, part); chains.add(part.as<JsonObjectConst>());
  }
}
void encodeNetwork(const NetworkSettings& source, JsonDocument& doc) {
  doc.clear(); auto n = doc.to<JsonObject>();
  n["ssid"] = source.ssid; n["password"] = source.password;
  n["oscHost"] = source.oscHost; n["oscPort"] = source.oscPort;
}
void encodeChain(const Chain& source, uint8_t id, JsonDocument& doc) {
    doc.clear(); auto c = doc.to<JsonObject>(); c["input"] = id;
    auto actions = c["actions"].to<JsonArray>();
    for (uint8_t k = 0; k < source.count; ++k) {
      const auto& a = source.actions[k]; auto j = actions.add<JsonObject>();
      j["delayMs"] = a.delayMs;
      if (a.protocol == Protocol::Wait) { j["protocol"] = "wait"; continue; }
      if (a.protocol == Protocol::Midi && allNotes(a.message)) {
        j["protocol"] = "midi";
        j["message"] = a.message == MidiMessage::AllNotesOn ? "allNotesOn" : "allNotesOff";
        if (a.message == MidiMessage::AllNotesOn) j["value"] = a.value;
        continue;
      }
      j["transport"] = a.transport == Transport::Wifi ? "wifi" : a.transport == Transport::Usb ? "usb" : a.transport == Transport::Ble ? "ble" : "both";
      if (a.protocol == Protocol::Osc) {
        j["protocol"] = "osc"; j["address"] = a.address;
        switch (a.oscType) {
          case OscType::Int: j["type"] = "int"; j["value"] = a.intValue; break;
          case OscType::Float: j["type"] = "float"; j["value"] = a.floatValue; break;
          case OscType::Bool: j["type"] = "bool"; j["value"] = a.boolValue; break;
          case OscType::String: j["type"] = "string"; j["value"] = a.stringValue; break;
        }
      } else if (a.protocol == Protocol::Midi) {
        j["protocol"] = "midi";
        j["message"] = a.message == MidiMessage::NoteOn ? "noteOn" : a.message == MidiMessage::NoteOff ? "noteOff" : "cc";
        j["channel"] = a.channel; j["number"] = a.number; j["value"] = a.value;
      } else {
        j["protocol"] = "keyboard"; j["usage"] = a.usage; j["modifiers"] = a.modifiers;
        j["message"] = a.keyMessage == KeyMessage::Down ? "keyDown" : a.keyMessage == KeyMessage::Up ? "keyUp" : "releaseAll";
      }
    }
}
} // namespace chimera
