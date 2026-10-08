#pragma once
#include <Arduino.h>

namespace chimera {
// Read-only diagnostics. No configuration JSON or credential values are printed.
void printDiagnostics(Print& out, const char* reason);
// Fixed-size startup/failure records; no allocation or printing from the hook.
void beginMemoryDiagnostics();
void memoryCheckpoint(const char* phase);
void memoryPhase(const char* phase);
void printMemoryDiagnostics(Print& out);
}
