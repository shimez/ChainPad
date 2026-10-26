#pragma once
#include <stddef.h>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_SPIRAM=4;
inline size_t heap_caps_get_free_size(unsigned){return 100000;}
inline size_t heap_caps_get_minimum_free_size(unsigned){return 90000;}
inline size_t heap_caps_get_largest_free_block(unsigned){return 50000;}
inline size_t heap_caps_get_total_size(unsigned){return 200000;}
struct multi_heap_info_t { size_t total_free_bytes, minimum_free_bytes, largest_free_block; };
namespace heapfake { inline unsigned samples=0; }
inline void heap_caps_get_info(multi_heap_info_t* info,unsigned caps) {
  ++heapfake::samples;
  const size_t base=(caps&MALLOC_CAP_SPIRAM)?8000000:80000;
  *info={base,base-1000,base-2000};
}
