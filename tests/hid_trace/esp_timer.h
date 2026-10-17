#pragma once
#include <atomic>
#include <cstdint>
inline std::atomic<int64_t> traceClock{0};
inline int64_t esp_timer_get_time() { return traceClock.load(); }
