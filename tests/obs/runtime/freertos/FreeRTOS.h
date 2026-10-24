#pragma once
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <cassert>
#include <chrono>
using StackType_t = uint32_t;
using portMUX_TYPE = std::recursive_mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#define pdMS_TO_TICKS(n) (n)
constexpr int pdTRUE=1, ARDUINO_RUNNING_CORE=1;
namespace rtos {
inline std::mutex mutex;
inline std::condition_variable cv;
inline std::thread thread;
inline bool stop=false;
inline unsigned permits=0, completed=0;
inline std::function<void()> beforeReceive;
struct Exit {};
inline void awaitTurn() {
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock,[]{return stop||permits;});
  if(stop)throw Exit{};
  --permits;
}
inline void step(unsigned ms=10) {
  std::unique_lock<std::mutex> lock(mutex);
  const auto target=completed+1;
  hostMillis+=ms; ++permits; cv.notify_all();
  assert(cv.wait_for(lock,std::chrono::seconds(5),[&]{return completed>=target;}));
}
inline void shutdown() {
  {std::lock_guard<std::mutex> lock(mutex);stop=true;cv.notify_all();}
  thread.join();
}
}
