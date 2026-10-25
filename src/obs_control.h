#pragma once
#include "obs_settings_store.h"
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
namespace chimera { namespace obscontrol {
enum class Apply { NotRequested, Accepted, Failed };
struct Reply { int http; bool saved; const char* error; };
// Single loop owner; no HTTP types or Worker internals. Never touches Config.
class Controller {
  obssettings::Store store_;
  Apply apply_ = Apply::NotRequested;
  bool stopped_ = true, globalAllowed_ = false;
  const char* error_ = nullptr;
  uint32_t lastLoop_ = 0, loopIntervalMaxUs_ = 0, saveMaxUs_ = 0;
  bool apply();
  void stop();
public:
  void begin();
  void observeLoop(uint32_t nowUs);
  void tick(bool globalAllowed);
  Reply put(const char* json, size_t size, bool maintenance);
  Reply reconnect(bool maintenance);
  void settingsJson(JsonObject out) const;
  void statusJson(JsonObject out) const;
  const char* applyName() const;
  const char* blockedReason() const;
};
} }
#endif
