#pragma once
#include <cassert>
#include "json_fault.h"
constexpr unsigned MALLOC_CAP_SPIRAM = 1u << 10, MALLOC_CAP_8BIT = 1u << 2;
inline void* heap_caps_malloc(size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return jsonFault::allocator.allocate(size);
}
inline void* heap_caps_realloc(void* ptr, size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return jsonFault::allocator.reallocate(ptr, size);
}
inline void heap_caps_free(void* ptr) { jsonFault::allocator.deallocate(ptr); }
