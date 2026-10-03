#include "transports.h"
#if CHAINPAD_HAS_USB
#include <USB.h>
#include <USBMIDI.h>
#include <USBHIDKeyboard.h>
#include <tusb.h>
#endif
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <atomic>

namespace chimera {
namespace {
#if CHAINPAD_HAS_USB
USBMIDI usbMidi("ChainPad MIDI");
USBHIDKeyboard usbKeyboard; // Registers the standard Arduino report descriptor.
#endif
constexpr char MIDI_SERVICE[] = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
constexpr char MIDI_CHAR[] = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
NimBLECharacteristic* midiCharacteristic = nullptr;
NimBLECharacteristic* keyboardInput = nullptr;
std::atomic<bool> midiSubscribed{false}, keyboardSubscribed{false};
std::atomic<uint32_t> midiGeneration{0}, keyboardGeneration{0}, usbGeneration{0};
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    server->updateConnParams(info.getConnHandle(), 6, 12, 0, 400);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    midiSubscribed = false; keyboardSubscribed = false;
    ++midiGeneration; ++keyboardGeneration;
  }
} serverCallbacks;
class SubscriptionCallbacks : public NimBLECharacteristicCallbacks {
 public:
  SubscriptionCallbacks(std::atomic<bool>& s, std::atomic<uint32_t>& g) : subscribed(s), generation(g) {}
 private:
  std::atomic<bool>& subscribed;
  std::atomic<uint32_t>& generation;
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t value) override {
    subscribed = (value & 1) != 0;
    ++generation;
  }
};
SubscriptionCallbacks midiCallbacks(midiSubscribed, midiGeneration);
SubscriptionCallbacks keyboardCallbacks(keyboardSubscribed, keyboardGeneration);
// F13..F24 are in the keyboard usage range through 0x73.
const uint8_t reportMap[] = {
  0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x85,0x01,
  0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
  0x75,0x01, 0x95,0x08, 0x81,0x02,
  0x95,0x01, 0x75,0x08, 0x81,0x01,
  0x95,0x05, 0x75,0x01, 0x05,0x08, 0x19,0x01, 0x29,0x05, 0x91,0x02,
  0x95,0x01, 0x75,0x03, 0x91,0x01,
  0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x73,
  0x05,0x07, 0x19,0x00, 0x29,0x73, 0x81,0x00, 0xc0
};
}
void transportsBegin() {
#if CHAINPAD_HAS_USB
  USB.productName("ChainPad Chimera");
  USB.manufacturerName("ChainPad");
  USB.onEvent([](void*, esp_event_base_t, int32_t id, void*) {
    if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_STOPPED_EVENT ||
        id == ARDUINO_USB_SUSPEND_EVENT || id == ARDUINO_USB_RESUME_EVENT) ++usbGeneration;
  });
  usbMidi.begin(); usbKeyboard.begin(); USB.begin();
#endif

  NimBLEDevice::init("ChainPad Chimera");
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  auto* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);
  auto* midiService = server->createService(MIDI_SERVICE);
  midiCharacteristic = midiService->createCharacteristic(MIDI_CHAR,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  midiCharacteristic->setCallbacks(&midiCallbacks);
  const uint8_t initial[] = {0x80, 0x80};
  midiCharacteristic->setValue(initial, sizeof(initial));
  auto* hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("ChainPad");
  // Populate the mandatory Device Information PnP characteristic using
  // this S3 development board's active USB identity.
#if CHAINPAD_HAS_USB
  hid->setPnp(0x02, USB.VID(), USB.PID(), 0x0100);
#else
  // Bluetooth SIG vendor source: Espressif Systems company identifier.
  hid->setPnp(0x01, 0x02e5, 0x0001, 0x0100);
#endif
  hid->setHidInfo(0, 1);
  hid->setReportMap(const_cast<uint8_t*>(reportMap), sizeof(reportMap));
  keyboardInput = hid->getInputReport(1);
  keyboardInput->setCallbacks(&keyboardCallbacks);
  const KeyboardReport neutral;
  keyboardInput->setValue(reinterpret_cast<const uint8_t*>(&neutral), sizeof(neutral));
  const uint8_t leds = 0;
  hid->getOutputReport(1)->setValue(&leds, sizeof(leds));
  hid->setBatteryLevel(100);
  server->start(); // Both services are registered before starting one server.

  // Legacy advertising is limited to 31 bytes. Keep both service UUIDs in
  // the primary packet and the device name in a separate scan response.
  NimBLEAdvertisementData advertisement;
  advertisement.setFlags(0x06);
  advertisement.addServiceUUID(MIDI_SERVICE);
  advertisement.addServiceUUID(hid->getHidService()->getUUID());
  advertisement.setAppearance(0x03c1);
  NimBLEAdvertisementData response;
  response.setName("ChainPad Chimera");
  auto* advertising = NimBLEDevice::getAdvertising();
  advertising->setAdvertisementData(advertisement);
  advertising->setScanResponseData(response);
  advertising->enableScanResponse(true);
  advertising->start();
}
void transportsTick() {
#if CHAINPAD_HAS_USB
  midiEventPacket_t packet;
  for (unsigned i = 0; i < 16 && usbMidi.readPacket(&packet); ++i) {}
#endif
}
bool midiReady(Transport t) {
#if CHAINPAD_HAS_USB
  if (t == Transport::Usb) return tud_midi_mounted() && !tud_suspended();
#endif
  return t == Transport::Ble && midiSubscribed.load();
}
bool keyboardReady(Transport t) {
#if CHAINPAD_HAS_USB
  if (t == Transport::Usb) return tud_mounted() && !tud_suspended();
#endif
  return t == Transport::Ble && keyboardSubscribed.load();
}
uint32_t midiEpoch(Transport t) { return t == Transport::Usb ? usbGeneration.load() : midiGeneration.load(); }
uint32_t keyboardEpoch(Transport t) { return t == Transport::Usb ? usbGeneration.load() : keyboardGeneration.load(); }
bool midiWrite(Transport t, uint8_t status, uint8_t data1, uint8_t data2) {
  if (!midiReady(t)) return false;
  if (t == Transport::Usb) {
#if CHAINPAD_HAS_USB
    midiEventPacket_t packet{static_cast<uint8_t>(status >> 4), status, data1, data2};
    return usbMidi.writePacket(&packet);
#else
    return false;
#endif
  }
  uint16_t stamp = millis() & 0x1fff;
  const uint8_t packet[] = {static_cast<uint8_t>(0x80 | (stamp >> 7)),
    static_cast<uint8_t>(0x80 | (stamp & 0x7f)), status, data1, data2};
  return midiCharacteristic->notify(packet, sizeof(packet));
}
bool keyboardWrite(Transport t, const KeyboardReport& report) {
  if (!keyboardReady(t)) return false;
  // USBHID::SendReport(timeout=0) can return false AFTER enqueueing a report
  // because it also waits for transfer completion. Use TinyUSB's nonblocking
  // enqueue result; only this loop task writes HID reports.
#if CHAINPAD_HAS_USB
  if (t == Transport::Usb) return tud_hid_ready() && tud_hid_report(HID_REPORT_ID_KEYBOARD, &report, sizeof(report));
#endif
  keyboardInput->setValue(reinterpret_cast<const uint8_t*>(&report), sizeof(report));
  return keyboardInput->notify();
}
}
