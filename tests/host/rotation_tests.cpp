#include "rotation.h"
#include "model.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace chimera;

void rotationTests() {
  RotationSettings<16> settings;
  assert(settings.mode == RotationMode::ActionChain && settings.outputCount == 0);
  assert(settings.axis.rangeSteps == 20 && settings.axis.initialPosition == 0);
  assert(validRotationSettings(settings));
  settings.mode = RotationMode::RotationValue;
  assert(validRotationSettings(settings)); // Empty outputs are valid in both modes.
  settings.outputCount = 17;
  assert(!validRotationSettings(settings));
  settings.outputCount = 1;
  settings.outputs[0].address[0] = 'x';
  settings.mode = RotationMode::ActionChain;
  assert(!validRotationSettings(settings)); // Inactive settings still validated.

  RotationAxis axis;
  uint32_t next = 99;
  axis.rangeSteps = 0;
  assert(!stepRotation(axis, 0, true, next) && next == 99);
  axis.rangeSteps = 65536;
  assert(!validRotationAxis(axis));
  axis.rangeSteps = 20; axis.initialPosition = 21;
  assert(!validRotationAxis(axis));
  axis.initialPosition = 0; axis.boundary = static_cast<RotationBoundary>(99);
  assert(!validRotationAxis(axis));

  RotationOutput output;
  RotationMappedValue value;
  for (uint32_t n : {1u, 20u, 65535u}) {
    axis = {n, 0, RotationBoundary::Stop};
    assert(stepRotation(axis, 0, false, next) && next == 0);
    assert(stepRotation(axis, n, true, next) && next == n);
    assert(stepRotation(axis, 0, true, next) && next == 1);
    assert(stepRotation(axis, n, false, next) && next == n - 1);
    assert(!validRotationPosition(axis, n + 1));
    axis.boundary = RotationBoundary::Wrap;
    assert(stepRotation(axis, n, true, next) && next == 0);
    assert(stepRotation(axis, 0, false, next) && next == n);
    uint32_t p = 0;
    for (uint32_t i = 0; i <= n; ++i) assert(stepRotation(axis, p, true, p));
    assert(p == 0);
    output.range.integer = {INT32_MIN, INT32_MAX};
    assert(mapRotation(axis, 0, output, value) && value.integer == INT32_MIN);
    assert(mapRotation(axis, n, output, value) && value.integer == INT32_MAX);
    int32_t previous = INT32_MIN;
    for (p = 0; p <= n; ++p) {
      assert(mapRotation(axis, p, output, value));
      // Independent floating reference: products remain exact integers in double.
      double reference = (double(INT32_MIN) * (n-p) + double(INT32_MAX) * p) / n;
      assert(value.integer == static_cast<int32_t>(std::round(reference)));
      assert(value.integer >= previous); previous = value.integer;
    }
    output.range.integer = {INT32_MAX, INT32_MIN};
    assert(mapRotation(axis, 0, output, value) && value.integer == INT32_MAX);
    assert(mapRotation(axis, n, output, value) && value.integer == INT32_MIN);
  }
  axis = {2, 0, RotationBoundary::Stop};
  for (auto range : {RotationIntRange{31,32}, RotationIntRange{-31,-32},
                     RotationIntRange{-1,0}, RotationIntRange{0,1},
                     RotationIntRange{INT32_MIN,INT32_MAX}}) {
    output.range.integer = range;
    assert(mapRotation(axis, 1, output, value));
    assert(value.integer == std::round((double(range.start) + range.end) / 2));
  }
  output.range.integer = {-7,-7};
  assert(mapRotation(axis, 1, output, value) && value.integer == -7);
  output.kind = RotationOutputKind::MidiCC; output.transport = Transport::Both;
  axis.rangeSteps = 20; output.range.integer = {0,127};
  assert(mapRotation(axis, 5, output, value) && value.integer == 32);
  axis.rangeSteps = 1000;
  assert(mapRotation(axis, 100, output, value) && value.integer == 13);
  assert(mapRotation(axis, 101, output, value) && value.integer == 13);
  output.range.integer.end = 128; assert(!validRotationOutput(output));
  output.range.integer = {127,0}; output.channel = 16; output.number = 127;
  assert(validRotationOutput(output));
  output.channel = 0; assert(!validRotationOutput(output));
  output.channel = 17; assert(!validRotationOutput(output));
  output.channel = 1; output.number = 128; assert(!validRotationOutput(output));

  output = RotationOutput{};
  std::memset(output.address, 'a', sizeof(output.address)); output.address[0] = '/';
  assert(!validRotationOutput(output));
  output.address[192] = 0; assert(validRotationOutput(output));
  output.address[1] = ' '; assert(!validRotationOutput(output));
  output = RotationOutput{}; output.kind = RotationOutputKind::OscFloat;
  output.range.floating = RotationFloatRange{0, 1};
  const float maximum = std::numeric_limits<float>::max();
  float normalized = 42;
  for (double bad : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN(), double(maximum)*2,
                     std::nextafter(double(maximum), std::numeric_limits<double>::infinity())}) {
    assert(!normalizeRotationFloat(bad, normalized) && normalized == 42);
  }
  assert(normalizeRotationFloat(0.1, normalized) && normalized == 0.1f);
  assert(normalizeRotationFloat(1e-100, normalized) && normalized == 0);
  assert(normalizeRotationFloat(-1e-100, normalized) && normalized == 0 && std::signbit(normalized));
  assert(normalizeRotationFloat(std::numeric_limits<float>::denorm_min(), normalized) && normalized > 0);
  assert(normalizeRotationFloat(maximum, normalized) && normalized == maximum);
  assert(normalizeRotationFloat(-maximum, normalized) && normalized == -maximum);
  for (uint32_t n : {1u, 2u, 65535u}) {
    axis.rangeSteps = n;
    output.range.floating = {-maximum, maximum};
    assert(mapRotation(axis, 0, output, value) && value.floating == -maximum);
    assert(mapRotation(axis, n, output, value) && value.floating == maximum);
    for (uint32_t p = 0; p <= n; ++p) {
      assert(mapRotation(axis, p, output, value) && std::isfinite(value.floating));
    }
  }
  axis.rangeSteps = 2;
  assert(mapRotation(axis, 1, output, value) && value.floating == 0);
  output.range.floating = {1,0};
  assert(mapRotation(axis, 1, output, value) && value.floating == 0.5f);
  output.range.floating = {0.1f,0.1f};
  assert(mapRotation(axis, 1, output, value) && value.floating == 0.1f);
  output.range.floating.start = std::numeric_limits<float>::infinity();
  value.floating = 42;
  assert(!mapRotation(axis, 1, output, value) && value.floating == 42);
  output.range.floating.start = std::numeric_limits<float>::quiet_NaN();
  assert(!validRotationOutput(output));
  output.range.floating.start = -std::numeric_limits<float>::infinity();
  assert(!validRotationOutput(output));
  output.range.floating = {-0.0f, 1.0f};
  assert(mapRotation(axis, 0, output, value) && std::signbit(value.floating));
  settings.outputCount = 1; settings.outputs[0] = output;
  auto copy = settings;
  settings.outputs[0].range.floating.end = 9;
  assert(validRotationSettings(copy) && copy.outputs[0].range.floating.end == 1);
  output = RotationOutput{}; axis.rangeSteps = 10;
  for (int32_t sign : {-1, 1}) {
    output.range.integer = {31 * sign, 32 * sign};
    for (uint32_t p : {4u, 5u, 6u}) {
      assert(mapRotation(axis, p, output, value));
      assert(value.integer == (p == 4 ? 31 : 32) * sign);
    }
  }
  printf("Rotation calculation tests passed: Axis=%zu Range=%zu Output=%zu Mapped=%zu Settings16=%zu Config=%zu\n",
    sizeof(RotationAxis), sizeof(RotationRange), sizeof(RotationOutput), sizeof(RotationMappedValue),
    sizeof(RotationSettings<16>), sizeof(Config));
}
