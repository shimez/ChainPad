#pragma once
#include "FreeRTOS.h"
#include <deque>
#include <vector>
#include <cstring>
struct StaticQueue_t { size_t count=0,size=0;std::deque<std::vector<uint8_t>> data;std::mutex mutex; };
using QueueHandle_t=StaticQueue_t*;
inline QueueHandle_t xQueueCreateStatic(size_t count,size_t size,uint8_t*,StaticQueue_t* q) {q->count=count;q->size=size;return q;}
inline int xQueueSend(QueueHandle_t q,const void* p,unsigned wait) {
  assert(wait==0);std::lock_guard<std::mutex> lock(q->mutex);
  if(q->data.size()==q->count)return 0;
  q->data.emplace_back(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+q->size);return 1;
}
inline int xQueueOverwrite(QueueHandle_t q,const void* p) {
  assert(q->count==1);std::lock_guard<std::mutex> lock(q->mutex);
  q->data.clear();q->data.emplace_back(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+q->size);return 1;
}
inline int xQueueReceive(QueueHandle_t q,void* p,unsigned wait) {
  assert(wait==0);
  if(q->count==4&&rtos::beforeReceive){auto fn=rtos::beforeReceive;rtos::beforeReceive={};fn();}
  std::lock_guard<std::mutex> lock(q->mutex);
  if(q->data.empty())return 0;
  memcpy(p,q->data.front().data(),q->size);q->data.pop_front();return 1;
}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t q) { std::lock_guard<std::mutex> lock(q->mutex);return unsigned(q->data.size()); }
