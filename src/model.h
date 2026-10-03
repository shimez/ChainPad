#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "capabilities.h"

namespace chimera {
constexpr uint8_t INPUT_COUNT = 28;
constexpr uint8_t MAX_ACTIONS = 8;
constexpr size_t MAX_CONFIG_BYTES = 48000;
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
struct Config {
  char ssid[33] = "";
  char password[65] = "";
  char oscHost[16] = "192.168.1.100"; // Numeric IPv4: no blocking DNS in dispatch.
  uint16_t oscPort = 9000;
  Chain chains[INPUT_COUNT];
};
extern Config config;
String inputName(uint8_t id);
void encodeConfig(const Config& source, JsonDocument& doc);
void encodeCapabilities(JsonDocument& doc);
bool decodeConfig(JsonVariantConst root, Config& out, String& error);
bool loadConfig(String& message);
bool saveConfig(const String& json, String& error);
bool saveWifiConfig(const String& json, String& error);
} // namespace chimera
