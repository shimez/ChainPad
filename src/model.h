#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "capabilities.h"

namespace chimera {
constexpr uint8_t INPUT_COUNT = 28;
constexpr uint8_t MAX_ACTIONS = 8;
constexpr size_t MAX_RECORD_BYTES = 16384;
enum class Protocol : uint8_t { Osc, Midi, Keyboard };
enum class Transport : uint8_t { Wifi, Usb, Ble, Both };
enum class OscType : uint8_t { Int, Float, Bool, String };
enum class MidiMessage : uint8_t { NoteOn, NoteOff, CC };
enum class KeyMessage : uint8_t { Down, Up, ReleaseAll };

// Reserved for a later scheduler. Phase 1 accepts only zero delay.
struct Action {
  Protocol protocol = Protocol::Osc;
  Transport transport = Transport::Wifi;
  uint32_t delayMs = 0;
  OscType oscType = OscType::Int;
  char address[97] = "/avatar/parameters/Costume";
  int32_t intValue = 2;
  float floatValue = 0;
  bool boolValue = false;
  char stringValue[65] = "";
  MidiMessage message = MidiMessage::NoteOn;
  KeyMessage keyMessage = KeyMessage::Down;
  uint8_t channel = 1;
  uint8_t number = 48;
  uint8_t value = 127;
  uint8_t usage = 104; // USB HID usage F13; independent of keyboard layout.
  uint8_t modifiers = 0;
};
struct Chain { uint8_t count = 0; Action actions[MAX_ACTIONS]; };
struct NetworkSettings {
  char ssid[33] = "";
  char password[65] = "";
  char oscHost[16] = "192.168.1.100"; // Numeric IPv4: no blocking DNS in dispatch.
  uint16_t oscPort = 9000;
};
struct Config : NetworkSettings {
  Chain chains[INPUT_COUNT];
};
extern Config config;
String inputName(uint8_t id);
void encodeConfig(const Config& source, JsonDocument& doc);
void encodeCapabilities(JsonDocument& doc);
bool decodeConfig(JsonVariantConst root, Config& out, String& error);
bool loadConfig(String& message);
bool saveWifiConfig(const String& json, String& error);
bool decodeNetwork(JsonVariantConst root, NetworkSettings& out, String& error);
bool decodeChain(JsonVariantConst root, uint8_t id, Chain& out, String& error);
void encodeNetwork(const NetworkSettings& source, JsonDocument& doc);
void encodeChain(const Chain& source, uint8_t id, JsonDocument& doc);
uint32_t beginConfigSave(const String& network, String& error);
bool stageConfigChain(uint32_t token, uint8_t id, const String& json, String& error);
bool commitConfigSave(uint32_t token, String& error);
} // namespace chimera
