#pragma once
#include "model.h"

namespace chimera {
struct KeyboardReport { uint8_t modifiers = 0, reserved = 0, keys[6] = {}; };
static_assert(sizeof(KeyboardReport) == 8, "HID report layout");
void transportsBegin();
void transportsTick();
bool midiReady(Transport t);
bool keyboardReady(Transport t);
uint32_t midiEpoch(Transport t);
uint32_t keyboardEpoch(Transport t);
bool midiWrite(Transport t, uint8_t status, uint8_t data1, uint8_t data2);
bool keyboardWrite(Transport t, const KeyboardReport& report);
}
