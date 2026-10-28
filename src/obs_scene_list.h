#pragma once
#include "obs_command.h"
namespace chimera { namespace obs {
constexpr size_t SCENE_COUNT_LIMIT = 32, SCENE_BYTES_LIMIT = 4096;
// Self-contained pool slot: no internal pointers. Names include their terminator.
struct SceneList {
  uint32_t id = 0, generation = 0;
  uint16_t count = 0, used = 0;
  uint16_t offsets[SCENE_COUNT_LIMIT]{};
  union {
    char names[SCENE_BYTES_LIMIT]{};
    struct { uint32_t low[SCENE_COUNT_LIMIT], high[SCENE_COUNT_LIMIT]; char names[SCENE_BYTES_LIMIT - 8 * SCENE_COUNT_LIMIT]; } items;
  };
  uint64_t itemId(unsigned i) const { return (uint64_t(items.high[i]) << 32) | items.low[i]; }
};
} }
