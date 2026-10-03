#include "inputs.h"
#include "hardware.h"
#include <esp_timer.h>
#include <atomic>

namespace chimera {
namespace {
QueueHandle_t events = nullptr;
TaskHandle_t scanner = nullptr;
esp_timer_handle_t timer = nullptr;
std::atomic<uint32_t> overflows{0};
std::atomic<uint16_t> pressed{0};
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t steps = 0;
volatile uint8_t previousAB = 0;
volatile int accumulator = 0;
DRAM_ATTR const int8_t transitions[] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
void ARDUINO_ISR_ATTR encoderInterrupt() {
  portENTER_CRITICAL_ISR(&encoderMux);
  uint8_t ab = (digitalRead(hardware::ENCODER_A) << 1) | digitalRead(hardware::ENCODER_B);
  if ((previousAB ^ ab) == 3) accumulator = 0;
  else accumulator = accumulator + transitions[(previousAB << 2) | ab];
  previousAB = ab;
  if (accumulator >= hardware::ENCODER_TRANSITIONS) {
    if (steps < 128) steps = steps + 1;
    accumulator = 0;
  } else if (accumulator <= -hardware::ENCODER_TRANSITIONS) {
    if (steps > -128) steps = steps - 1;
    accumulator = 0;
  }
  portEXIT_CRITICAL_ISR(&encoderMux);
}
void emit(uint8_t id, uint32_t now) {
  InputEvent event{id, now};
  if (xQueueSend(events, &event, 0) != pdTRUE) ++overflows;
}
struct Switch {
  bool candidate = false, stable = false;
  uint32_t since = 0;
  void sample(bool raw, uint8_t key, uint32_t now) {
    if (raw != candidate) { candidate = raw; since = now; }
    if (stable != candidate && now - since >= hardware::DEBOUNCE_MS) {
      stable = candidate;
      emit(key * 2 + (stable ? 0 : 1), now);
    }
  }
};
void scanTask(void*) {
  Switch keys[13];
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint32_t now = millis();
    for (unsigned row = 0; row < 4; ++row) {
      digitalWrite(hardware::ROWS[row], LOW);
      delayMicroseconds(3);
      for (unsigned col = 0; col < 3; ++col) {
        uint8_t key = row * 3 + col;
        keys[key].sample(digitalRead(hardware::COLS[col]) == LOW, key, now);
      }
      // Inactive rows float. This avoids output contention on simultaneous keys.
      digitalWrite(hardware::ROWS[row], HIGH);
    }
    keys[12].sample(digitalRead(hardware::PUSH) == LOW, 12, now);
    uint16_t mask = 0;
    for (unsigned i = 0; i < 13; ++i) if (keys[i].stable) mask |= 1u << i;
    pressed = mask;
    portENTER_CRITICAL(&encoderMux);
    int32_t movement = steps; steps = 0;
    portEXIT_CRITICAL(&encoderMux);
    while (movement > 0) { emit(26, now); --movement; }
    while (movement < 0) { emit(27, now); ++movement; }
  }
}
void scanTick(void*) { if (scanner) xTaskNotifyGive(scanner); }
}
bool inputsBegin() {
  events = xQueueCreate(128, sizeof(InputEvent));
  if (!events) return false;
  for (auto pin : hardware::ROWS) { digitalWrite(pin, HIGH); pinMode(pin, OUTPUT_OPEN_DRAIN); }
  for (auto pin : hardware::COLS) pinMode(pin, INPUT_PULLUP);
  pinMode(hardware::ENCODER_A, INPUT_PULLUP); pinMode(hardware::ENCODER_B, INPUT_PULLUP);
  pinMode(hardware::PUSH, INPUT_PULLUP); pinMode(hardware::LED, OUTPUT);
  setStatusLed(false);
  previousAB = (digitalRead(hardware::ENCODER_A) << 1) | digitalRead(hardware::ENCODER_B);
  attachInterrupt(hardware::ENCODER_A, encoderInterrupt, CHANGE);
  attachInterrupt(hardware::ENCODER_B, encoderInterrupt, CHANGE);
  if (xTaskCreatePinnedToCore(scanTask, "input-scan", 3072, nullptr, 2, &scanner, ARDUINO_RUNNING_CORE) != pdPASS) return false;
  esp_timer_create_args_t args{};
  args.callback = scanTick; args.name = "input-tick"; args.skip_unhandled_events = true;
  if (esp_timer_create(&args, &timer) != ESP_OK) return false;
  return esp_timer_start_periodic(timer, 1000) == ESP_OK;
}
bool nextInput(InputEvent& e) { return events && xQueueReceive(events, &e, 0) == pdTRUE; }
void discardInputs() { if (events) xQueueReset(events); }
uint32_t inputOverflows() { return overflows.load(); }
uint16_t pressedInputs() { return pressed.load(); }
void setStatusLed(bool on) { digitalWrite(hardware::LED, on == hardware::LED_ACTIVE_HIGH ? HIGH : LOW); }
}
