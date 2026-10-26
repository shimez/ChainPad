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
obs::Operation requestedKind = obs::Operation::GetSceneList, cachedKind = obs::Operation::GetSceneList;
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
  if (!result.success) { sceneError = result.code == -10 ? "listLimitOrInvalid" : result.code == -11 ? "audioCapabilitiesUnavailable" : result.code == -12 ? "actionPriorityOrTimeout" : "requestFailed"; sceneStale = true; return; }
  const auto* next = obsTakeSceneList(result.id, obsStatusSnapshot().generation);
  if (!next) {
    sceneStale = true; sceneError = "resultUnavailable"; return;
  }
  sceneCache = next;
  cachedKind = requestedKind;
  sceneAvailable = true; sceneStale = false; sceneError = nullptr; sceneUpdatedAt = millis();
  sceneTick();
}
}
#endif
static SendResult namesRefresh(obs::Operation kind) {
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
  sceneTick();
  if (scenePending) return requestedKind == kind ? SendResult::Accepted : SendResult::Busy;
  if (obsStatusSnapshot().queued) return SendResult::Busy;
  requestedKind = kind;
  obs::Command command; command.operation = kind;
  const auto result = obsDispatch(command, scenePending);
  sceneStale = true;
  sceneError = result == SendResult::Accepted ? nullptr : result == SendResult::Busy ? "busy" : "unavailable";
  if (result == SendResult::Accepted) sceneRequestedAt = millis();
  return result;
#else
  (void)kind;
  return SendResult::Unavailable;
#endif
}
static void namesJson(JsonObject json, obs::Operation kind) {
  const char* field = kind == obs::Operation::GetInputList ? "inputs" : "scenes";
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
  sceneTick();
  const bool available = sceneAvailable && cachedKind == kind;
  const bool requested = requestedKind == kind;
  json["state"] = requested && scenePending ? "loading" : requested && sceneError ? "error" : !available ? "unavailable" : sceneStale ? "stale" : "ready";
  json["complete"] = available && !sceneStale;
  json["requestId"] = requested ? scenePending : 0;
  json["generation"] = obsStatusSnapshot().generation;
  json["error"] = requested ? sceneError : nullptr;
  if (available) json["ageMs"] = uint32_t(millis() - sceneUpdatedAt); else json["ageMs"] = nullptr;
  auto names = json[field].to<JsonArray>();
  if (available) for (unsigned i = 0; i < sceneCache->count; ++i) names.add(sceneCache->names + sceneCache->offsets[i]);
#else
  json["state"] = "unsupported"; json["complete"] = false; json[field].to<JsonArray>();
#endif
}
SendResult obsScenesRefresh() { return namesRefresh(obs::Operation::GetSceneList); }
SendResult obsInputsRefresh() { return namesRefresh(obs::Operation::GetInputList); }
void obsScenesJson(JsonObject json) { namesJson(json, obs::Operation::GetSceneList); }
void obsInputsJson(JsonObject json) { namesJson(json, obs::Operation::GetInputList); }
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
    if (obs::discovery(result.operation)) { sceneResult(result); continue; }
    status.last = result;
    if (result.success) ++status.completed; else ++status.failed;
  }
  sceneTick();
#endif
}
}
