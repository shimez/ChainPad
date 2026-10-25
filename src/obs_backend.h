#pragma once
#include "backends.h"
#include "obs_protocol.h"

namespace chimera {
// Volatile Phase 1 settings, never serialized. All public calls (including
// status) run on the Arduino loop. Worker owns its copy and socket operations.
struct ObsSettings {
  char ip[16]{};
  uint16_t port = 4455;
  char password[129]{};
  bool enabled = false;
};
struct ObsResult { uint32_t id = 0; bool success = false; int code = 0; obs::Operation operation = obs::Operation::SetCurrentProgramScene; };
struct ObsStatus {
  obs::State state = obs::State::Disabled;
  uint32_t accepted = 0, rejected = 0, completed = 0, failed = 0, discarded = 0;
  uint32_t resultDrops = 0, attempts = 0, ignored = 0, queued = 0;
  uint32_t stackLowBytes = 0, loopMaxUs = 0, connectMs = 0, authMs = 0, responseMs = 0;
  int lastError = 0;
  uint32_t lastClose = 0;
  uint32_t generation = 0; // Connection generation; cache freshness only.
};
#if CHAINPAD_HAS_OBS
void obsBegin();
bool obsApplySettings(const ObsSettings& snapshot); // Also explicitly retries terminal errors.
SendResult obsDispatch(const char* scene, uint32_t& requestId);
SendResult obsDispatch(const obs::Command&, uint32_t& requestId);
void obsPanic();
void obsTick(bool allowed); // Permission + loop metrics only; never network I/O.
ObsStatus obsStatusSnapshot(); // Approximate independent counters, no secret data.
bool obsNextResult(ObsResult& result);
bool obsTakeSceneList(obs::SceneList&);
#else
inline void obsBegin() {}
inline bool obsApplySettings(const ObsSettings&) { return false; }
inline SendResult obsDispatch(const char*, uint32_t& id) { id = 0; return SendResult::Unavailable; }
inline SendResult obsDispatch(const obs::Command&, uint32_t& id) { id = 0; return SendResult::Unavailable; }
inline void obsPanic() {}
inline void obsTick(bool) {}
inline ObsStatus obsStatusSnapshot() { return {}; }
inline bool obsNextResult(ObsResult&) { return false; }
inline bool obsTakeSceneList(obs::SceneList&) { return false; }
#endif
}
