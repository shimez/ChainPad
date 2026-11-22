#pragma once

#if CHAINPAD_SAVE_MEM
#include <Arduino.h>
namespace chimera::savemem {
enum Point { StageDecoded, StageEncoded, FileWritten, RotationEncoded, ValidateChain,
             ApplyChain, ValidateRotation, ApplyRotation, Count };
void arm();
void print(Print&);
void sample(Point, unsigned);
}
#define SAVE_SAMPLE(point, id) ::chimera::savemem::sample(::chimera::savemem::point, id)
#else
#define SAVE_SAMPLE(point, id) ((void)0)
#endif
