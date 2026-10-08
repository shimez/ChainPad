#include "model.h"
#include <IPAddress.h>
#include <cmath>
#include <memory>
#include <new>
#include <cstdio>
#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CHAINPAD_HOST_TEST)
#include <esp_heap_caps.h>
#include <cstdlib>
#if !CONFIG_SPIRAM_BOOT_INIT
#error "S3 Config allocation requires PSRAM initialization before C++ constructors"
#endif
#endif

namespace chimera {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CHAINPAD_HOST_TEST)
namespace {
Config& allocateConfig() {
  // The S3 SDK initializes PSRAM before global constructors. Keep the full
  // fixed-capacity Config out of internal RAM needed by Wi-Fi/USB/BLE.
  void* storage = heap_caps_malloc(sizeof(Config), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!storage) std::abort();
  return *new (storage) Config{};
}
}
Config& config = allocateConfig();
#else
Config config;
#endif
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
  if (!root.is<JsonObjectConst>() || !integer(root["schemaVersion"], 2, 2)) return false;
  if (!decodeRotation(root["encoderRotation"], out.encoderRotation, error)) return false;
  if (!decodeNetwork(root["network"], out, error)) return false;
  auto chains = root["chains"].as<JsonArrayConst>();
  error = "Expected exactly 28 uniquely identified input chains";
  if (chains.size() != INPUT_COUNT) return false;
  bool seen[INPUT_COUNT] = {};
  uint8_t counts[INPUT_COUNT] = {};
  // Validate all pair budgets before any write to the shared Action pools.
  for (auto j : chains) {
    if (!integer(j["input"], 0, INPUT_COUNT - 1)) return false;
    uint8_t id = j["input"];
    if (seen[id]) return false;
    seen[id] = true;
    if (!j["actions"].is<JsonArrayConst>() || j["actions"].size() > eventActionLimit(id)) return false;
    counts[id] = j["actions"].size();
  }
  for (uint8_t id = 0; id < 26; id += 2) {
    if (counts[id] + counts[id + 1] > MAX_KEY_ACTIONS) {
      error = "Press / Release total exceeds 16 Actions at " + inputName(id); return false;
    }
  }
  std::unique_ptr<Chain> chain(new (std::nothrow) Chain);
  if (!chain) { error = "Not enough RAM for chain decode"; return false; }
  for (auto j : chains) {
    uint8_t id = j["input"];
    if (!decodeChain(j, id, *chain, error)) return false;
    out.chains[id].assign(*chain);
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
  doc["schemaVersion"] = 2;
  doc["rotationOutputCapacity"] = ROTATION_OUTPUT_CAPACITY;
  doc["rotationRuntimeSupported"] = true;
  doc["rotationOutputSendingSupported"] = true;
  auto midi = doc["midiTransports"].to<JsonArray>();
  if (HAS_USB_MIDI) { midi.add("both"); midi.add("usb"); }
  midi.add("ble");
  auto keyboard = doc["keyboardTransports"].to<JsonArray>();
  if (HAS_USB_KEYBOARD) keyboard.add("usb");
  keyboard.add("ble");
}
void encodeConfig(const Config& source, JsonDocument& doc) {
  doc.clear(); doc["schemaVersion"] = 2;
  JsonDocument part; encodeNetwork(source, part); doc["network"] = part;
  encodeRotation(source.encoderRotation, part); doc["encoderRotation"] = part;
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
template<typename T> void encodeChainImpl(const T& source, uint8_t id, JsonDocument& doc) {
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
void encodeChain(const Chain& source, uint8_t id, JsonDocument& doc) { encodeChainImpl(source, id, doc); }
void encodeChain(const ChainView& source, uint8_t id, JsonDocument& doc) { encodeChainImpl(source, id, doc); }
bool decodeRotation(JsonVariantConst root, EncoderRotationSettings& out, String& error) {
  error = "Invalid encoder rotation settings";
  if (!root.is<JsonObjectConst>()) return false;
  const bool chainMode = root["mode"] == "actionChain";
  if (!chainMode && root["mode"] != "rotationValue") return false;
  auto value = root["rotationValue"];
  if (!value.is<JsonObjectConst>() || !integer(value["rangeSteps"], 1, MAX_RANGE_STEPS) ||
      !integer(value["initialPosition"], 0, value["rangeSteps"].as<uint32_t>())) return false;
  if (value["boundary"] != "stop" && value["boundary"] != "wrap") return false;
  if (!value["outputs"].is<JsonArrayConst>() || value["outputs"].size() > ROTATION_OUTPUT_CAPACITY) return false;
  // Callers use scratch settings, never an active Config during a transaction.
  out.mode = chainMode ? RotationMode::ActionChain : RotationMode::RotationValue;
  out.axis = {value["rangeSteps"].as<uint32_t>(), value["initialPosition"].as<uint32_t>(),
    value["boundary"] == "stop" ? RotationBoundary::Stop : RotationBoundary::Wrap};
  out.outputCount = 0;
  for (auto j : value["outputs"].as<JsonArrayConst>()) {
    RotationOutput output;
    if (!j.is<JsonObjectConst>()) return false;
    if (j["protocol"] == "osc") {
      if (j["transport"] != "wifi" || !text(j["address"], output.address, sizeof(output.address))) return false;
      if (j["type"] == "int") {
        if (!integer(j["start"], INT32_MIN, INT32_MAX) || !integer(j["end"], INT32_MIN, INT32_MAX)) return false;
        output.range.integer = {j["start"].as<int32_t>(), j["end"].as<int32_t>()};
      } else if (j["type"] == "float") {
        if (!j["start"].is<double>() || !j["end"].is<double>()) return false;
        RotationFloatRange range;
        if (!normalizeRotationFloat(j["start"].as<double>(), range.start) ||
            !normalizeRotationFloat(j["end"].as<double>(), range.end)) return false;
        output.kind = RotationOutputKind::OscFloat; output.range.floating = range;
      } else return false;
    } else if (j["protocol"] == "midi" && j["message"] == "cc") {
      if (j["transport"] != "usb" && j["transport"] != "ble" && j["transport"] != "both") return false;
      if (!integer(j["channel"], 1, 16) || !integer(j["number"], 0, 127) ||
          !integer(j["start"], 0, 127) || !integer(j["end"], 0, 127)) return false;
      output.kind = RotationOutputKind::MidiCC;
      output.transport = !HAS_USB_MIDI || j["transport"] == "ble" ? Transport::Ble :
        j["transport"] == "usb" ? Transport::Usb : Transport::Both;
      output.channel = j["channel"]; output.number = j["number"];
      output.range.integer = {j["start"].as<int32_t>(), j["end"].as<int32_t>()};
    } else return false;
    if (!validRotationOutput(output)) return false;
    out.outputs[out.outputCount++] = output;
  }
  error = ""; return true;
}
void encodeRotation(const EncoderRotationSettings& source, JsonDocument& doc, bool losslessWire) {
  doc.clear();
  doc["mode"] = source.mode == RotationMode::ActionChain ? "actionChain" : "rotationValue";
  auto value = doc["rotationValue"].to<JsonObject>();
  value["rangeSteps"] = source.axis.rangeSteps;
  value["initialPosition"] = source.axis.initialPosition;
  value["boundary"] = source.axis.boundary == RotationBoundary::Stop ? "stop" : "wrap";
  auto outputs = value["outputs"].to<JsonArray>();
  for (uint32_t i = 0; i < source.outputCount; ++i) {
    const auto& output = source.outputs[i]; auto j = outputs.add<JsonObject>();
    if (output.kind == RotationOutputKind::MidiCC) {
      j["protocol"] = "midi"; j["message"] = "cc";
      j["transport"] = output.transport == Transport::Usb ? "usb" : output.transport == Transport::Ble ? "ble" : "both";
      j["channel"] = output.channel; j["number"] = output.number;
    } else {
      j["protocol"] = "osc"; j["transport"] = "wifi"; j["address"] = output.address;
      j["type"] = output.kind == RotationOutputKind::OscFloat ? "float" : "int";
    }
    if (output.kind == RotationOutputKind::OscFloat) {
      if (losslessWire) {
        // ArduinoJson compresses exact doubles back to float and serializes only
        // 7 significant digits. Seventeen digits preserve the promoted float32
        // value without emitting a decimal above FLT_MAX (e.g. %.9g would).
        char start[32], end[32];
        snprintf(start, sizeof(start), "%.17g", double(output.range.floating.start));
        snprintf(end, sizeof(end), "%.17g", double(output.range.floating.end));
        if (output.range.floating.start == 0 && std::signbit(output.range.floating.start)) strcpy(start, "-0.0");
        if (output.range.floating.end == 0 && std::signbit(output.range.floating.end)) strcpy(end, "-0.0");
        j["start"] = serialized(String(start)); j["end"] = serialized(String(end));
      } else {
        j["start"] = output.range.floating.start; j["end"] = output.range.floating.end;
      }
    } else {
      j["start"] = output.range.integer.start; j["end"] = output.range.integer.end;
    }
  }
}
} // namespace chimera
