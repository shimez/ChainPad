#pragma once
#include "FreeRTOS.h"
struct StaticTask_t {};
using TaskHandle_t = StaticTask_t*;
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void(*fn)(void*), const char*, unsigned stack, void*, int priority, StackType_t*, StaticTask_t* control, int) {
  assert(stack==8192&&priority==1);
  rtos::thread=std::thread([fn]{try{rtos::awaitTurn();fn(nullptr);}catch(rtos::Exit&) {}});
  return control;
}
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 3500; }
inline void vTaskDelay(unsigned) {
  {std::lock_guard<std::mutex> lock(rtos::mutex);++rtos::completed;rtos::cv.notify_all();}
  rtos::awaitTurn();
}
