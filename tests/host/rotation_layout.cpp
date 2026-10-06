// Compile-only target ABI probe; inspect symbol sizes with target nm -S.
#include "rotation.h"
#include "model.h"
#include "rotation_runtime.h"
using namespace chimera;
extern "C" {
char rotation_axis_size[sizeof(RotationAxis)];
char rotation_range_size[sizeof(RotationRange)];
char rotation_output_size[sizeof(RotationOutput)];
char rotation_mapped_size[sizeof(RotationMappedValue)];
char rotation_runtime_size[sizeof(RotationRuntime)];
char rotation_settings16_size[sizeof(RotationSettings<16>)];
char integrated_config_size[sizeof(Config)];
}
