#include "obs_control.h"
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
#include <memory>
#include <new>
namespace chimera { namespace obscontrol {
using namespace obssettings;
namespace {
const char* storageError(State state) {
  switch (state) {
    case State::Corrupt: return "corrupt";
    case State::UnsupportedVersion: return "unsupportedVersion";
    case State::IoError: return "ioError";
    default: return nullptr;
  }
}
int httpError(Error e) {
  switch (e) {
    case Error::InvalidInput: case Error::TooLarge: case Error::PasswordRequired: return 400;
    case Error::ReplaceRequired: return 409;
    default: return 500;
  }
}
const char* connectionName(obs::State state) {
  switch (state) {
    case obs::State::Disabled: return "Disabled";
    case obs::State::Disconnected: return "Disconnected";
    case obs::State::Connecting: return "Connecting";
    case obs::State::Authenticating: return "Authenticating";
    case obs::State::Ready: return "Ready";
    default: return "Error";
  }
}
}
const char* Controller::applyName() const {
  return apply_ == Apply::Accepted ? "accepted" : apply_ == Apply::Failed ? "failed" : "notRequested";
}
void Controller::stop() { stopped_ = true; obsTick(false); }
void Controller::observeLoop(uint32_t nowUs) {
  if (lastLoop_ && uint32_t(nowUs - lastLoop_) > loopIntervalMaxUs_) loopIntervalMaxUs_ = nowUs - lastLoop_;
  lastLoop_ = nowUs;
}
void Controller::tick(bool globalAllowed) {
  globalAllowed_ = globalAllowed;
  obsTick(globalAllowed_ && store_.settings() && apply_ == Apply::Accepted && !stopped_);
}
bool Controller::apply() {
  const auto* settings = store_.settings();
  if (!settings || !obsApplySettings(*settings)) {
    apply_ = Apply::Failed; error_ = "applyFailed"; stop(); return false;
  }
  apply_ = Apply::Accepted; stopped_ = false; error_ = nullptr;
  tick(globalAllowed_); return true;
}
void Controller::begin() {
  stop(); apply_ = Apply::NotRequested;
  Error error; store_.load(error);
  error_ = storageError(store_.state());
  if (store_.settings()) apply(); // Missing sends disabled defaults, never writes.
}
Reply Controller::put(const char* json, size_t size, bool maintenance) {
  if (maintenance) return {409, false, "maintenance"};
  if (size > MaxJsonBytes) return {400, false, "tooLarge"};
  std::unique_ptr<char[]> update(new (std::nothrow) char[MaxJsonBytes + 1]);
  if (!update) return {500, false, "noMemory"};
  size_t count; bool replace;
  auto error = unwrapRequest(json, size, update.get(), MaxJsonBytes + 1, count, replace);
  if (error == Error::None) {
    const uint32_t start = micros();
    error = store_.save(update.get(), count, replace);
    const uint32_t elapsed = micros() - start;
    if (elapsed > saveMaxUs_) saveMaxUs_ = elapsed;
  }
  volatile char* p = update.get(); for (size_t i = 0; i <= count && i <= MaxJsonBytes; ++i) p[i] = 0;
  if (error != Error::None) {
    error_ = errorName(error);
    if (error == Error::Rename) {
      // Failed PUT remains failed even when load finds a valid new active file.
      // No automatic apply: explicit reconnect must use this reloaded snapshot.
      stop(); apply_ = Apply::NotRequested;
      Error readError; store_.load(readError);
    } else if (!store_.settings()) { stop(); apply_ = Apply::NotRequested; }
    return {httpError(error), false, errorName(error)};
  }
  return apply() ? Reply{200, true, nullptr} : Reply{503, true, "applyFailed"};
}
Reply Controller::reconnect(bool maintenance) {
  if (maintenance) return {409, false, "maintenance"};
  const auto* settings = store_.settings();
  if (!settings) return {409, false, "settingsUnavailable"};
  if (!settings->enabled) return {409, false, "disabled"};
  return apply() ? Reply{202, false, nullptr} : Reply{503, false, "applyFailed"};
}
const char* Controller::blockedReason() const {
  if (!store_.settings()) return "settingsUnavailable";
  if (apply_ == Apply::Failed) return "applyFailed";
  if (stopped_ || apply_ != Apply::Accepted) return "reapplyRequired";
  if (!store_.settings()->enabled) return "disabled";
  if (!globalAllowed_) return "globalOutputsBlocked";
  return nullptr;
}
void Controller::settingsJson(JsonObject out) const {
  out["ok"] = true;
  out["storageState"] = stateName(store_.state());
  out["settingsAvailable"] = store_.settings() != nullptr;
  out["settings"] = nullptr;
  if (const auto* s = store_.settings()) {
    auto value = out["settings"].to<JsonObject>();
    value["enabled"] = s->enabled; value["ip"] = s->ip; value["port"] = s->port;
    value["passwordConfigured"] = s->password[0] != 0;
  }
  out["applyState"] = applyName();
  out["error"] = nullptr;
  const char* code = storageError(store_.state());
  if (!code) code = error_;
  if (code) {
    auto error = out["error"].to<JsonObject>();
    error["code"] = code; error["message"] = "OBS settings operation requires attention";
  }
}
void Controller::statusJson(JsonObject out) const {
  const auto status = obsStatusSnapshot(); // Existing snapshot masks stale-generation Ready.
  out["storageState"] = stateName(store_.state()); out["applyState"] = applyName();
  const char* reason = blockedReason();
  out["blockedReason"] = nullptr; if (reason) out["blockedReason"] = reason;
  out["stopRequested"] = reason != nullptr;
  out["connectionState"] = connectionName(status.state);
  out["generation"] = status.generation;
  out["lastError"] = nullptr; if (status.lastError) out["lastError"] = status.lastError;
  out["lastClose"] = nullptr; if (status.lastClose) out["lastClose"] = status.lastClose;
  out["loopIntervalMaxUs"] = loopIntervalMaxUs_; out["saveMaxUs"] = saveMaxUs_;
}
} }
#endif
