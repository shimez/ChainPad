#include "model.h"
#include <Preferences.h>
#include <IPAddress.h>
#include <cmath>
#include <memory>

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
  if (!j.is<JsonObjectConst>() || !integer(j["delayMs"], 0, 0)) return false;
  a.delayMs = j["delayMs"];
  String p = j["protocol"] | "";
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
  auto n = root["network"];
  if (!text(n["ssid"], out.ssid, sizeof(out.ssid)) || !text(n["password"], out.password, sizeof(out.password)) ||
      !text(n["oscHost"], out.oscHost, sizeof(out.oscHost)) || !integer(n["oscPort"], 1, 65535)) return false;
  IPAddress ip;
  if (!ip.fromString(out.oscHost)) return false;
  out.oscPort = n["oscPort"];
  auto chains = root["chains"].as<JsonArrayConst>();
  error = "Expected exactly 28 uniquely identified input chains";
  if (chains.size() != INPUT_COUNT) return false;
  bool seen[INPUT_COUNT] = {};
  for (auto j : chains) {
    if (!integer(j["input"], 0, INPUT_COUNT - 1)) return false;
    uint8_t id = j["input"];
    if (seen[id]) return false;
    seen[id] = true;
    auto actions = j["actions"].as<JsonArrayConst>();
    error = "Invalid action list at " + inputName(id);
    if (!j["actions"].is<JsonArrayConst>() || actions.size() > MAX_ACTIONS) return false;
    out.chains[id].count = 0;
    for (auto action : actions) {
      Action a;
      error = "Invalid action " + String(out.chains[id].count + 1) + " at " + inputName(id);
      if (!parseAction(action, a)) return false;
      out.chains[id].actions[out.chains[id].count++] = a;
    }
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
  auto midi = doc["midiTransports"].to<JsonArray>();
  if (HAS_USB_MIDI) { midi.add("both"); midi.add("usb"); }
  midi.add("ble");
  auto keyboard = doc["keyboardTransports"].to<JsonArray>();
  if (HAS_USB_KEYBOARD) keyboard.add("usb");
  keyboard.add("ble");
}
void encodeConfig(const Config& source, JsonDocument& doc) {
  doc.clear(); doc["schemaVersion"] = 1;
  auto n = doc["network"].to<JsonObject>();
  n["ssid"] = source.ssid; n["password"] = source.password;
  n["oscHost"] = source.oscHost; n["oscPort"] = source.oscPort;
  auto chains = doc["chains"].to<JsonArray>();
  for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
    auto c = chains.add<JsonObject>(); c["input"] = i;
    auto actions = c["actions"].to<JsonArray>();
    for (uint8_t k = 0; k < source.chains[i].count; ++k) {
      const auto& a = source.chains[i].actions[k]; auto j = actions.add<JsonObject>();
      j["delayMs"] = a.delayMs;
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
}
namespace {
bool persistDocument(JsonDocument& doc, String& error) {
  if (doc.overflowed()) { error = "Insufficient JSON memory"; return false; }
  String canonical;
  if (serializeJson(doc, canonical) != measureJson(doc) || canonical.length() > MAX_CONFIG_BYTES) {
    error = "Serialized config exceeds memory/size limit"; return false;
  }
  // Release the JSON tree before NVS allocates its own write buffers.
  doc.clear();
  Preferences prefs;
  if (!prefs.begin("chainpad", false, "config_nvs")) { error = "NVS open failed"; return false; }
  // NVS strings are limited to one page (~4 KB). A blob spans pages.
  size_t written = prefs.putBytes("config", canonical.c_str(), canonical.length());
  prefs.end();
  if (written != canonical.length()) { error = "NVS write failed; config not applied"; return false; }
  error = "";
  return true;
}
}
bool saveWifiConfig(const String& json, String& error) {
  // Only credentials change: no ~44 KB Config copy or full-config reparse.
  if (json.length() > 1024) { error = "Wi-Fi request too large"; return false; }
  JsonDocument doc;
  if (deserializeJson(doc, json)) { error = "Invalid Wi-Fi JSON"; return false; }
  char ssid[sizeof(config.ssid)]{}, password[sizeof(config.password)]{};
  if (!text(doc["ssid"], ssid, sizeof(ssid)) || !text(doc["password"], password, sizeof(password))) {
    error = "Invalid SSID / Password"; return false;
  }
  encodeConfig(config, doc);
  doc["network"]["ssid"] = ssid;
  doc["network"]["password"] = password;
  if (!persistDocument(doc, error)) return false;
  memcpy(config.ssid, ssid, sizeof(ssid));
  memcpy(config.password, password, sizeof(password));
  return true;
}
bool saveConfig(const String& json, String& error) {
  if (json.length() > MAX_CONFIG_BYTES) { error = "Config exceeds 48000 bytes"; return false; }
  JsonDocument doc;
  if (deserializeJson(doc, json)) { error = "Invalid JSON"; return false; }
  auto next = std::unique_ptr<Config>(new (std::nothrow) Config);
  if (!next) { error = "Insufficient memory"; return false; }
  if (!decodeConfig(doc.as<JsonVariantConst>(), *next, error)) return false;
  // Store canonical JSON as a single NVS blob; NVS commits atomically.
  encodeConfig(*next, doc);
  if (!persistDocument(doc, error)) return false;
  config = *next;
  return true;
}
bool loadConfig(String& message) {
  Preferences prefs;
  if (!prefs.begin("chainpad", false, "config_nvs")) { message = "NVS unavailable"; return false; }
  size_t size = prefs.getBytesLength("config");
  if (!size) { prefs.end(); message = "Factory config: all chains empty"; return true; }
  if (size > MAX_CONFIG_BYTES) { prefs.end(); message = "Saved config exceeds size limit"; return false; }
  auto json = std::unique_ptr<char[]>(new (std::nothrow) char[size]);
  if (!json || prefs.getBytes("config", json.get(), size) != size) {
    prefs.end(); message = "Config read failed"; return false;
  }
  prefs.end();
  JsonDocument doc;
  auto next = std::unique_ptr<Config>(new (std::nothrow) Config);
  if (!next || deserializeJson(doc, json.get(), size) || !decodeConfig(doc.as<JsonVariantConst>(), *next, message)) {
    message = "Saved config invalid; using empty chains. " + message; return false;
  }
  config = *next; message = "Loaded saved config"; return true;
}
} // namespace chimera
