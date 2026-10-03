#pragma once
#include "engine.h"

namespace chimera {
bool inputsBegin();
bool nextInput(InputEvent& event);
void discardInputs();
uint32_t inputOverflows();
uint16_t pressedInputs();
void setStatusLed(bool on);
}
