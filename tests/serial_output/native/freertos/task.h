#pragma once
#include <cstdint>
using TaskHandle_t = void*;
inline TaskHandle_t xTaskGetHandle(const char*) { return reinterpret_cast<void*>(1); }
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return UINT32_MAX; }
