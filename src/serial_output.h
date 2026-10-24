#pragma once
#include "capabilities.h"
#include <Arduino.h>
#include <utility>

#if (defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CHAINPAD_HOST_TEST)) || defined(CHAINPAD_SERIAL_NATIVE_TEST)
#define CHAINPAD_SERIAL_BUFFERED 1
#else
#define CHAINPAD_SERIAL_BUFFERED 0
#endif

namespace chimera {
#if CHAINPAD_SERIAL_BUFFERED
namespace serialout {
constexpr size_t Capacity = 4096, PumpBytes = 64;
constexpr size_t DiagnosticBudget = 2560, MemoryBudget = 3072, StatusBudget = 768;
constexpr size_t BurstBudget = 1536, ResultBudget = 64, TraceBudget = 384;
struct Stats {
  uint32_t txPartial = 0, txZero = 0, outputRejected = 0, formatRejected = 0;
  uint32_t commandReplyDrops = 0, resultDeferred = 0;
  uint32_t queuedBytes = 0, maxQueuedBytes = 0, pumpMaxUs = 0;
};
// All operations belong to setup/Arduino loop, including the USB sink.
class Output : public Print {
  uint8_t bytes_[Capacity]{};
  size_t head_ = 0, count_ = 0, reserved_ = 0, staged_ = 0;
  bool building_ = false, bad_ = false, notice_ = false;
  bool connected_ = false, everConnected_ = false, boundary_ = false;
  size_t boundaryOffset_ = 0;
  Stats stats_;
public:
  using Print::write;
  bool begin(size_t budget, bool reportRejection = true);
  bool commit();
  void cancel();
  void formatFailure();
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t size) override;
  void pump(Print& sink, bool connected, bool paused = false);
  Stats stats() const { auto s = stats_; s.queuedBytes = uint32_t(count_); return s; }
  void replyDropped() { ++stats_.commandReplyDrops; notice_ = true; }
  void resultDeferred() { ++stats_.resultDeferred; }
};
Output& output();
void printStats(Print& out);
void printStats(Print& out, const Stats& stats);
}
// Bounded formatting. Unlike Print::printf, failures are visible to commit().
void outputPrintf(Print& out, const char* format, ...);
template<class F> bool serialReport(Print&, size_t budget, F&& produce) {
  auto& out = serialout::output();
  if (!out.begin(budget)) return false;
  produce(static_cast<Print&>(out));
  return out.commit();
}
template<class F> void serialOperation(Print&, size_t budget, F&& execute) {
  auto& out = serialout::output();
  if (out.begin(budget)) {
    const bool operated = execute(static_cast<Print&>(out));
    if (!out.commit() && operated) out.replyDropped();
  } else {
    class Discard : public Print {
    public:
      size_t write(uint8_t) override { return 1; }
      size_t write(const uint8_t*, size_t size) override { return size; }
    } discard;
    if (execute(static_cast<Print&>(discard))) out.replyDropped();
  }
}
#else
template<class... Args> void outputPrintf(Print& out, const char* format, Args&&... args) {
  out.printf(format, std::forward<Args>(args)...);
}
template<class F> bool serialReport(Print& out, size_t, F&& produce) { produce(out); return true; }
template<class F> void serialOperation(Print& out, size_t, F&& execute) { execute(out); }
#endif
}
