#pragma once
#include "rotation.h"

namespace chimera {
// Runtime only: never serialized. Actual changes publish to the separate Latest-State sender.
class RotationRuntime {
public:
  void restart(const EncoderRotationSettings& settings, bool available = true);
  void apply(const EncoderRotationSettings& settings, bool available = true);
  bool step(bool clockwise);
  bool active() const { return enabled; }
  uint32_t position() const { return current; }
  uint32_t rangeSteps() const { return axis.rangeSteps; }
private:
  RotationAxis axis;
  uint32_t current = 0;
  bool enabled = false;
};
extern RotationRuntime rotationRuntime;
} // namespace chimera
