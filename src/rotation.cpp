#include "rotation.h"
#include <cmath>
#include <limits>

namespace chimera {
bool validRotationAxis(const RotationAxis& axis) {
  return axis.rangeSteps >= 1 && axis.rangeSteps <= MAX_RANGE_STEPS &&
    axis.initialPosition <= axis.rangeSteps &&
    (axis.boundary == RotationBoundary::Stop || axis.boundary == RotationBoundary::Wrap);
}
bool validRotationPosition(const RotationAxis& axis, uint32_t position) {
  return validRotationAxis(axis) && position <= axis.rangeSteps;
}
bool normalizeRotationFloat(double value, float& result) {
  const double maximum = std::numeric_limits<float>::max();
  if (!std::isfinite(value) || value < -maximum || value > maximum) return false;
  result = static_cast<float>(value);
  return true;
}
bool validRotationOutput(const RotationOutput& output) {
  if (output.kind == RotationOutputKind::MidiCC) {
    return (output.transport == Transport::Usb || output.transport == Transport::Ble || output.transport == Transport::Both) &&
      output.channel >= 1 && output.channel <= 16 && output.number <= 127 &&
      output.range.integer.start >= 0 && output.range.integer.start <= 127 &&
      output.range.integer.end >= 0 && output.range.integer.end <= 127;
  }
  if (output.kind != RotationOutputKind::OscInt && output.kind != RotationOutputKind::OscFloat) return false;
  if (output.transport != Transport::Wifi || output.address[0] != '/') return false;
  // Same byte-oriented address rules as Action, but bounded even for malformed records.
  bool terminated = false;
  for (size_t i = 0; i < sizeof(output.address); ++i) {
    const uint8_t c = static_cast<uint8_t>(output.address[i]);
    if (!c) { terminated = true; break; }
    if (c <= 32) return false;
  }
  return terminated && (output.kind != RotationOutputKind::OscFloat ||
    (std::isfinite(output.range.floating.start) && std::isfinite(output.range.floating.end)));
}
bool stepRotation(const RotationAxis& axis, uint32_t position, bool clockwise, uint32_t& result) {
  if (!validRotationPosition(axis, position)) return false;
  if (clockwise) result = position < axis.rangeSteps ? position + 1 :
    (axis.boundary == RotationBoundary::Wrap ? 0 : position);
  else result = position > 0 ? position - 1 :
    (axis.boundary == RotationBoundary::Wrap ? axis.rangeSteps : position);
  return true;
}
bool mapRotation(const RotationAxis& axis, uint32_t position,
                 const RotationOutput& output, RotationMappedValue& result) {
  if (!validRotationPosition(axis, position) || !validRotationOutput(output)) return false;
  RotationMappedValue mapped;
  mapped.kind = output.kind;
  if (output.kind == RotationOutputKind::OscFloat) {
    const auto& r = output.range.floating;
    if (!position) mapped.floating = r.start;
    else if (position == axis.rangeSteps) mapped.floating = r.end;
    else mapped.floating = static_cast<float>(double(r.start) +
      (double(r.end) - double(r.start)) * (double(position) / axis.rangeSteps));
  } else {
    const auto& r = output.range.integer;
    if (!position) mapped.integer = r.start;
    else if (position == axis.rangeSteps) mapped.integer = r.end;
    else {
      const int64_t numerator = int64_t(r.start) * (axis.rangeSteps - position) + int64_t(r.end) * position;
      const uint64_t magnitude = numerator < 0 ? uint64_t(-numerator) : uint64_t(numerator);
      int64_t rounded = magnitude / axis.rangeSteps;
      if (2 * (magnitude % axis.rangeSteps) >= axis.rangeSteps) ++rounded;
      mapped.integer = static_cast<int32_t>(numerator < 0 ? -rounded : rounded);
    }
  }
  result = mapped;
  return true;
}
} // namespace chimera
