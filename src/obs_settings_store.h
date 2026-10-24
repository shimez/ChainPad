#pragma once
#include "obs_settings.h"
#if CHAINPAD_HAS_OBS
namespace chimera { namespace obssettings {
// Single setup/loop owner. Never mounts, formats, touches Config or calls Worker.
// settings() is null until a successful load/save (including verified Missing).
class Store {
  ObsSettings saved_;
  State state_ = State::IoError;
public:
  State state() const { return state_; }
  const ObsSettings* settings() const { return state_ == State::Ready || state_ == State::Missing ? &saved_ : nullptr; }
  State load(Error& error);
  Error save(const char* json, size_t size, bool replaceInvalid = false);
};
} }
#endif
