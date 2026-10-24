#include "../../../src/obs_settings_store.h"
#if CHAINPAD_HAS_OBS
namespace { chimera::obssettings::Store stored; }
// Linker root only; deliberately has no caller or static constructor doing I/O.
extern "C" size_t chainpadObsSettingsLinkProbe(unsigned op, const char* json, size_t size, char* out, size_t capacity) {
  using namespace chimera::obssettings;
  Error error;
  if (op == 0) return unsigned(stored.load(error));
  if (op == 1) return unsigned(stored.save(json, size, false));
  if (op == 2) return unsigned(stored.save(json, size, true));
  if (op == 3 && stored.settings()) return encodePublic(*stored.settings(), out, capacity);
  if (op == 4) return stateName(stored.state())[0];
  return errorName(Error(op))[0];
}
#endif
