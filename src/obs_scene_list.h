#pragma once
#include "obs_command.h"
namespace chimera { namespace obs {
constexpr size_t SCENE_COUNT_LIMIT = 32, SCENE_BYTES_LIMIT = 4096;
// Self-contained pool slot: no internal pointers. Names include their terminator.
struct SceneList {
  uint32_t id = 0, generation = 0;
  uint16_t count = 0, used = 0;
  uint16_t offsets[SCENE_COUNT_LIMIT]{};
  char names[SCENE_BYTES_LIMIT]{};
};
} }
