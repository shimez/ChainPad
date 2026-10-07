#include "rotation_sender.h"
#include "backend_internal.h"
#include <WiFi.h>
#include <atomic>

namespace chimera {
RotationSendStats rotationSendStats;
#ifdef CHAINPAD_ROTATION_TEST
bool rotationTestHeld = false;
#endif
namespace {
struct Pending {
  union { int32_t integer; float floating; } value{};
  uint32_t generation = 0;
  bool valid = false;
};
Pending pending[ROTATION_OUTPUT_CAPACITY][ROTATION_ROUTES];
struct Connection { uint32_t epoch = 0; bool ready = false; } connections[ROTATION_ROUTES];
std::atomic<uint32_t> wifiEpoch{0};
unsigned cursor = 0;
constexpr unsigned SLOT_COUNT = ROTATION_OUTPUT_CAPACITY * ROTATION_ROUTES;
Transport routeTransport(unsigned route) { return route == 1 ? Transport::Usb : Transport::Ble; }
void discard(Pending& p) {
  if (p.valid) { p.valid = false; ++rotationSendStats.discarded; }
}
void sync() {
  for (unsigned route = 0; route < ROTATION_ROUTES; ++route) {
    const auto transport = routeTransport(route);
    const uint32_t epoch = route ? midiEpoch(transport) : wifiEpoch.load();
    const bool ready = route ? midiReady(transport) :
      (WiFi.status() == WL_CONNECTED || WiFi.softAPgetStationNum() > 0);
    auto& c = connections[route];
    if (c.epoch != epoch || c.ready != ready || !ready) {
      for (auto& output : pending) discard(output[route]);
      c = {epoch, ready};
    }
  }
}
}
void rotationWifiChanged() { ++wifiEpoch; }
void rotationSenderBegin() {
#ifndef CHAINPAD_HOST_TEST
  WiFi.onEvent([](WiFiEvent_t event) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      case ARDUINO_EVENT_WIFI_AP_STOP:
        rotationWifiChanged(); break;
      default: break;
    }
  });
#endif
}
void rotationDiscard() { for (auto& output : pending) for (auto& p : output) discard(p); }
unsigned rotationPendingCount() {
  unsigned count = 0;
  for (const auto& output : pending) for (const auto& p : output) count += p.valid;
  return count;
}
size_t rotationPendingBytes() { return sizeof(pending); }
size_t rotationSenderBytes() {
  return sizeof(pending) + sizeof(connections) + sizeof(wifiEpoch) + sizeof(cursor) + sizeof(rotationSendStats);
}
void rotationPublish(uint32_t position) {
  sync();
  const auto& settings = config.encoderRotation;
  ++rotationSendStats.generation; // RAM-only modulo counter; valid bits do not depend on its value.
  rotationSendStats.snapshot = position;
  for (unsigned i = 0; i < settings.outputCount; ++i) {
    const auto& output = settings.outputs[i];
    RotationMappedValue value;
    if (!mapRotation(settings.axis, position, output, value)) continue;
    for (unsigned route = 0; route < ROTATION_ROUTES; ++route) {
      const bool target = output.kind == RotationOutputKind::MidiCC ?
        (route && (output.transport == Transport::Both || output.transport == routeTransport(route))) : route == 0;
      if (!target) continue;
      if (!connections[route].ready) { ++rotationSendStats.unavailable; continue; }
      auto& p = pending[i][route];
      if (p.valid) ++rotationSendStats.overwritten;
      if (output.kind == RotationOutputKind::OscFloat) p.value.floating = value.floating;
      else p.value.integer = value.integer;
      p.generation = rotationSendStats.generation;
      p.valid = true;
    }
  }
}
void rotationSendTick() {
  sync();
#ifdef CHAINPAD_ROTATION_TEST
  if (rotationTestHeld) return; // Observe real connection epochs even while held.
#endif
  unsigned budget = ROTATION_SEND_BUDGET;
  for (unsigned scanned = 0; scanned < SLOT_COUNT && budget; ++scanned) {
    const unsigned slot = cursor;
    cursor = (cursor + 1) % SLOT_COUNT;
    const unsigned i = slot / ROTATION_ROUTES, route = slot % ROTATION_ROUTES;
    auto& p = pending[i][route];
    if (!p.valid) continue;
    sync(); // A preceding API call may have overlapped a connection callback.
    if (!p.valid) continue;
    // Never append Rotation CC to the ordinary FIFO or bypass its queued messages.
    if (route && midiQueued(routeTransport(route))) continue;
    --budget;
    const auto& output = config.encoderRotation.outputs[i];
    p.valid = false; // One API attempt only; failures are never retried.
    bool accepted;
    if (route) accepted = midiWrite(routeTransport(route), 0xb0 | (output.channel - 1), output.number, p.value.integer);
    else {
      Action action;
      action.protocol = Protocol::Osc;
      memcpy(action.address, output.address, sizeof(action.address));
      action.oscType = output.kind == RotationOutputKind::OscFloat ? OscType::Float : OscType::Int;
      if (action.oscType == OscType::Float) action.floatValue = p.value.floating;
      else action.intValue = p.value.integer;
      accepted = oscDispatch(action) == SendResult::Accepted;
    }
    if (accepted) ++rotationSendStats.accepted[route];
    else { ++rotationSendStats.failed[route]; ++rotationSendStats.discarded; }
  }
}
} // namespace chimera
