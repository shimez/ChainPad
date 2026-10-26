#include "obs_actions.h"
#include "obs_scenes.h"
#include <cstring>
namespace chimera {
namespace { ObsActionStatus status; }
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
namespace {
const obs::SceneList* sceneCache = nullptr;
uint32_t scenePending = 0, sceneRequestedAt = 0, sceneUpdatedAt = 0;
bool sceneAvailable = false, sceneStale = true;
const char* sceneError = nullptr;
void sceneTick() {
  const auto connection = obsStatusSnapshot();
  if (connection.state != obs::State::Ready || !sceneCache || connection.generation != sceneCache->generation ||
      uint32_t(millis() - sceneUpdatedAt) >= 60000) sceneStale = true;
  // Covers dropped diagnostic results as well as delayed queue processing.
  if (scenePending && uint32_t(millis() - sceneRequestedAt) >= 20000) {
    scenePending = 0; sceneStale = true; sceneError = "timeout";
  }
}
void sceneResult(const ObsResult& result) {
  if (result.id != scenePending) return;
  scenePending = 0;
  if (!result.success) { sceneError = result.code == -10 ? "listLimitOrInvalid" : "requestFailed"; sceneStale = true; return; }
  const auto* next = obsTakeSceneList(result.id, obsStatusSnapshot().generation);
  if (!next) {
    sceneStale = true; sceneError = "resultUnavailable"; return;
  }
  sceneCache = next;
  sceneAvailable = true; sceneStale = false; sceneError = nullptr; sceneUpdatedAt = millis();
  sceneTick();
}
}
#endif
SendResult obsScenesRefresh() {
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
  sceneTick();
  if (scenePending) return SendResult::Accepted; // Coalesce browser refreshes.
  obs::Command command; command.operation = obs::Operation::GetSceneList;
  const auto result = obsDispatch(command, scenePending);
  sceneStale = true;
  sceneError = result == SendResult::Accepted ? nullptr : result == SendResult::Busy ? "busy" : "unavailable";
  if (result == SendResult::Accepted) sceneRequestedAt = millis();
  return result;
#else
  return SendResult::Unavailable;
#endif
}
void obsScenesJson(JsonObject json) {
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
  sceneTick();
  json["state"] = scenePending ? "loading" : sceneError ? "error" : !sceneAvailable ? "unavailable" : sceneStale ? "stale" : "ready";
  json["complete"] = sceneAvailable && !sceneStale;
  json["requestId"] = scenePending;
  json["generation"] = obsStatusSnapshot().generation;
  json["error"] = sceneError;
  if (sceneAvailable) json["ageMs"] = uint32_t(millis() - sceneUpdatedAt); else json["ageMs"] = nullptr;
  auto names = json["scenes"].to<JsonArray>();
  if (sceneAvailable) for (unsigned i = 0; i < sceneCache->count; ++i) names.add(sceneCache->names + sceneCache->offsets[i]);
#else
  json["state"] = "unsupported"; json["complete"] = false; json["scenes"].to<JsonArray>();
#endif
}
const ObsActionStatus& obsActionStatus() { return status; }
SendResult obsActionDispatch(const Action& a) {
  status.requestId = 0;
#if !CHAINPAD_HAS_OBS
  (void)a; status.admission = "unsupported"; return SendResult::Unavailable;
#elif defined(CHAINPAD_OBS_TEST)
  // Test console retains exclusive ownership of its settings and result FIFO.
  (void)a; status.admission = "testConsoleOwned"; return SendResult::Unavailable;
#else
  obs::Command command; command.operation = a.obsOperation; command.muted = a.boolValue;
  if (obs::named(command.operation)) memcpy(command.target, a.obsTarget, sizeof(command.target));
  const auto result = obsDispatch(command, status.requestId);
  switch (result) {
    case SendResult::Accepted: status.admission = "accepted"; break;
    case SendResult::Busy: status.admission = "busy"; break;
    case SendResult::Failed: status.admission = "failed"; break;
    case SendResult::Unavailable:
      status.admission = obsStatusSnapshot().state == obs::State::Disabled ? "disabled" : "disconnected"; break;
  }
  return result;
#endif
}
void obsActionsTick() {
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
  ObsResult result;
  // Bounded work per loop, sufficient to empty the existing eight-entry FIFO.
  for (unsigned n = 0; n < 8 && obsNextResult(result); ++n) {
    if (result.operation == obs::Operation::GetSceneList) { sceneResult(result); continue; }
    status.last = result;
    if (result.success) ++status.completed; else ++status.failed;
  }
  sceneTick();
#endif
}
}
