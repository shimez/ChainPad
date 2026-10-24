#pragma once
#include <stddef.h>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_SPIRAM=4;
inline size_t heap_caps_get_free_size(unsigned){return 100000;}
inline size_t heap_caps_get_minimum_free_size(unsigned){return 90000;}
inline size_t heap_caps_get_largest_free_block(unsigned){return 50000;}
inline size_t heap_caps_get_total_size(unsigned){return 200000;}
