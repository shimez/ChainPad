#include "rotation_runtime.h"
#include "rotation_sender.h"

namespace chimera {
RotationRuntime rotationRuntime;
void RotationRuntime::restart(const EncoderRotationSettings& settings, bool available) {
  enabled = false; current = 0;
  apply(settings, available);
}
void RotationRuntime::apply(const EncoderRotationSettings& settings, bool available) {
  rotationDiscard();
  const bool next = available && settings.mode == RotationMode::RotationValue && validRotationAxis(settings.axis);
  if (next && (!enabled || axis.rangeSteps != settings.axis.rangeSteps)) current = settings.axis.initialPosition;
  if (!next) current = 0;
  axis = settings.axis;
  enabled = next;
}
bool RotationRuntime::step(bool clockwise) {
  if (!enabled) return false;
  const uint32_t previous = current;
  const bool valid = stepRotation(axis, current, clockwise, current);
  if (valid && current != previous) rotationPublish(current);
  return valid;
}
} // namespace chimera
