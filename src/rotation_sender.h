#pragma once
#include "rotation.h"

namespace chimera {
constexpr unsigned ROTATION_SEND_BUDGET = 4;
constexpr unsigned ROTATION_ROUTES = 3; // Wi-Fi, USB MIDI, BLE MIDI
struct RotationSendStats {
  uint32_t generation = 0, snapshot = 0;
  uint32_t overwritten = 0, discarded = 0, unavailable = 0;
  uint32_t accepted[ROTATION_ROUTES]{}, failed[ROTATION_ROUTES]{};
};
extern RotationSendStats rotationSendStats;
void rotationSenderBegin();
void rotationWifiChanged(); // Callback-safe: only increments an atomic epoch.
void rotationPublish(uint32_t position);
void rotationSendTick(); // Once per loop, after ordinary input/Action processing.
void rotationDiscard();
unsigned rotationPendingCount();
size_t rotationPendingBytes();
size_t rotationSenderBytes();
#ifdef CHAINPAD_ROTATION_TEST
// Hardware fault-injection build only. No endpoint/state in production builds.
extern bool rotationTestHeld;
#endif
} // namespace chimera
