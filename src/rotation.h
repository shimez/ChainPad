#pragma once
#include "model_types.h"

namespace chimera {
constexpr uint32_t MAX_RANGE_STEPS = 65535;
constexpr size_t ROTATION_OUTPUT_CAPACITY = 16; // Provisional hardware evaluation capacity.
enum class RotationMode : uint8_t { ActionChain, RotationValue };
enum class RotationBoundary : uint8_t { Stop, Wrap };
enum class RotationOutputKind : uint8_t { OscInt, OscFloat, MidiCC };

struct RotationAxis {
  uint32_t rangeSteps = 20;
  uint32_t initialPosition = 0;
  RotationBoundary boundary = RotationBoundary::Stop;
};
// Trivial aggregates allow assignment to activate either union member safely.
struct RotationIntRange { int32_t start, end; };
struct RotationFloatRange { float start, end; };
union RotationRange {
  RotationIntRange integer;
  RotationFloatRange floating;
  RotationRange() : integer{0, 127} {}
};
// A dedicated value-output record, with no Action/Wait/HID/String payload.
// For OscFloat, activate range.floating by assigning RotationFloatRange{0, 1}.
struct RotationOutput {
  RotationOutputKind kind = RotationOutputKind::OscInt;
  Transport transport = Transport::Wifi;
  uint8_t channel = 1;
  uint8_t number = 7;
  char address[MAX_OSC_ADDRESS_BYTES + 1] = "/avatar/parameters/Costume";
  RotationRange range;
};
// Capacity is deliberately a template parameter until hardware evaluation.
template<size_t Capacity> struct RotationSettings {
  RotationMode mode = RotationMode::ActionChain;
  RotationAxis axis;
  uint32_t outputCount = 0;
  RotationOutput outputs[Capacity];
};
struct RotationMappedValue {
  RotationOutputKind kind = RotationOutputKind::OscInt;
  int32_t integer = 0;
  float floating = 0;
};
using EncoderRotationSettings = RotationSettings<ROTATION_OUTPUT_CAPACITY>;

bool validRotationAxis(const RotationAxis& axis);
bool validRotationPosition(const RotationAxis& axis, uint32_t position);
bool normalizeRotationFloat(double value, float& result);
bool validRotationOutput(const RotationOutput& output);
// One delivered CW/CCW event. Invalid inputs leave result unchanged.
bool stepRotation(const RotationAxis& axis, uint32_t position, bool clockwise, uint32_t& result);
bool mapRotation(const RotationAxis& axis, uint32_t position,
                 const RotationOutput& output, RotationMappedValue& result);
template<size_t Capacity> bool validRotationSettings(const RotationSettings<Capacity>& settings) {
  if (settings.mode != RotationMode::ActionChain && settings.mode != RotationMode::RotationValue) return false;
  if (!validRotationAxis(settings.axis) || settings.outputCount > Capacity) return false;
  for (uint32_t i = 0; i < settings.outputCount; ++i)
    if (!validRotationOutput(settings.outputs[i])) return false;
  return true;
}
} // namespace chimera
