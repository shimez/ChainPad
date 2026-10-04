#pragma once
#include <Arduino.h>

namespace chimera {
// Read-only diagnostics. No configuration JSON or credential values are printed.
void printDiagnostics(Print& out, const char* reason);
}
