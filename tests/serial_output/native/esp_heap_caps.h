#pragma once
#include <cstddef>
#include <cstdint>
constexpr unsigned MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_SPIRAM=4;
inline uint32_t heapValue = UINT32_MAX;
inline unsigned heapQueries = 0;
inline size_t heap_caps_get_free_size(unsigned) { ++heapQueries; return heapValue; }
inline size_t heap_caps_get_minimum_free_size(unsigned) { ++heapQueries; return heapValue; }
inline size_t heap_caps_get_largest_free_block(unsigned) { ++heapQueries; return heapValue; }
inline size_t heap_caps_get_total_size(unsigned) { ++heapQueries; return heapValue; }
struct multi_heap_info_t { size_t total_free_bytes, minimum_free_bytes, largest_free_block, total_allocated_bytes; };
inline void heap_caps_get_info(multi_heap_info_t* p, unsigned) { ++heapQueries; *p = {heapValue,heapValue,heapValue,heapValue}; }
inline void heap_caps_register_failed_alloc_callback(void(*)(size_t,uint32_t,const char*)) {}
