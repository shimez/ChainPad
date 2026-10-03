#pragma once
#include <Arduino.h>

namespace chimera::hardware {
// Current ChainOSCPad PCB. XIAO board variants map D0..D10 to each SoC's GPIOs.
constexpr uint8_t ROWS[] = {D0, D1, D2, D3};
constexpr uint8_t COLS[] = {D4, D5, D6};
constexpr uint8_t ENCODER_A = D7, ENCODER_B = D8, PUSH = D9, LED = D10;
constexpr uint32_t DEBOUNCE_MS = 10;
constexpr int ENCODER_TRANSITIONS = 4;
constexpr bool LED_ACTIVE_HIGH = true;
}
