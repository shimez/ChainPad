#pragma once
#if defined(CONFIG_IDF_TARGET_ESP32C5)
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

namespace chimera {
// Used only by GET /api/config. Leave internal DMA-capable memory to Wi-Fi
// while the current record is being sent. Never fall back to internal RAM.
class ConfigGetAllocator final : public ArduinoJson::Allocator {
  static constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
public:
  void* allocate(size_t size) override { return heap_caps_malloc(size, caps); }
  void* reallocate(void* ptr, size_t size) override { return heap_caps_realloc(ptr, size, caps); }
  void deallocate(void* ptr) override { heap_caps_free(ptr); }
};
}
#endif
