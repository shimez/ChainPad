#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "capabilities.h"

namespace chimera {
constexpr uint8_t INPUT_COUNT = 28;
constexpr uint8_t MAX_ACTIONS = 16;
constexpr uint8_t MAX_KEY_ACTIONS = 16;
constexpr uint16_t MAX_TOTAL_ACTIONS = 224;
constexpr size_t MAX_OSC_ADDRESS_BYTES = 192;
constexpr size_t MAX_OSC_STRING_BYTES = 128;
constexpr uint8_t eventActionLimit(uint8_t id) { return id < 26 ? MAX_ACTIONS : 8; }
constexpr size_t MAX_RECORD_BYTES = 24576; // 16 maximum OSC Actions, including JSON escaping.
constexpr uint32_t MAX_WAIT_MS = 86400000; // 24 hours; safely below half the millis() range.
enum class Protocol : uint8_t { Osc, Midi, Keyboard, Wait };
enum class Transport : uint8_t { Wifi, Usb, Ble, Both };
enum class OscType : uint8_t { Int, Float, Bool, String };
enum class MidiMessage : uint8_t { NoteOn, NoteOff, CC, AllNotesOn, AllNotesOff };
inline bool allNotes(MidiMessage m) { return m == MidiMessage::AllNotesOn || m == MidiMessage::AllNotesOff; }
enum class KeyMessage : uint8_t { Down, Up, ReleaseAll };

// delayMs is the duration of an explicit Wait Action; other Actions use zero.
struct Action {
  Protocol protocol = Protocol::Osc;
  Transport transport = Transport::Wifi;
  uint32_t delayMs = 0;
  OscType oscType = OscType::Int;
  char address[MAX_OSC_ADDRESS_BYTES + 1] = "/avatar/parameters/Costume";
  int32_t intValue = 2;
  float floatValue = 0;
  bool boolValue = false;
  char stringValue[MAX_OSC_STRING_BYTES + 1] = "";
  MidiMessage message = MidiMessage::NoteOn;
  KeyMessage keyMessage = KeyMessage::Down;
  uint8_t channel = 1;
  uint8_t number = 48;
  uint8_t value = 127;
  uint8_t usage = 104; // USB HID usage F13; independent of keyboard layout.
  uint8_t modifiers = 0;
};
struct Chain { uint8_t count = 0; Action actions[MAX_ACTIONS]; };
// Press grows from the front, Release from the back of the SAME 16-slot pool.
// Logical indexing preserves each Event's configured order without moving its peer.
struct ActionView {
  Action* first = nullptr;
  int8_t step = 1;
  Action& operator[](size_t index) { return first[int(index) * step]; }
  const Action& operator[](size_t index) const { return first[int(index) * step]; }
};
struct ChainView {
  uint8_t count = 0;
  ActionView actions;
  template<typename T> void assign(const T& source) {
    count = source.count;
    for (uint8_t i = 0; i < count; ++i) actions[i] = source.actions[i];
  }
  ChainView& operator=(const ChainView& source) {
    if (this != &source) assign(source);
    return *this;
  }
};
struct NetworkSettings {
  char ssid[33] = "";
  char password[65] = "";
  char oscHost[16] = "192.168.1.100"; // Numeric IPv4: no blocking DNS in dispatch.
  uint16_t oscPort = 9000;
};
struct Config : NetworkSettings {
  ChainView chains[INPUT_COUNT];
  Config() {
    for (uint8_t i = 0; i < 26; ++i)
      chains[i].actions = {slots + (i / 2) * MAX_KEY_ACTIONS + (i % 2 ? MAX_KEY_ACTIONS - 1 : 0), int8_t(i % 2 ? -1 : 1)};
    chains[26].actions = {slots + 13 * MAX_KEY_ACTIONS, 1};
    chains[27].actions = {slots + 13 * MAX_KEY_ACTIONS + 8, 1};
  }
  Config(const Config& source) : Config() { *this = source; }
  Config& operator=(const Config& source) {
    if (this == &source) return *this;
    static_cast<NetworkSettings&>(*this) = source;
    for (size_t i = 0; i < MAX_TOTAL_ACTIONS; ++i) slots[i] = source.slots[i];
    for (uint8_t i = 0; i < INPUT_COUNT; ++i) chains[i].count = source.chains[i].count;
    return *this;
  }
private:
  Action slots[MAX_TOTAL_ACTIONS];
};
extern Config config;
String inputName(uint8_t id);
void encodeConfig(const Config& source, JsonDocument& doc);
void encodeCapabilities(JsonDocument& doc);
bool decodeConfig(JsonVariantConst root, Config& out, String& error);
bool loadConfig(String& message);
bool configStorageMounted();
bool saveWifiConfig(const String& json, String& error);
bool decodeNetwork(JsonVariantConst root, NetworkSettings& out, String& error);
bool decodeChain(JsonVariantConst root, uint8_t id, Chain& out, String& error);
void encodeNetwork(const NetworkSettings& source, JsonDocument& doc);
void encodeChain(const Chain& source, uint8_t id, JsonDocument& doc);
void encodeChain(const ChainView& source, uint8_t id, JsonDocument& doc);
uint32_t beginConfigSave(const String& network, String& error);
bool stageConfigChain(uint32_t token, uint8_t id, const String& json, String& error);
bool commitConfigSave(uint32_t token, String& error);
} // namespace chimera
