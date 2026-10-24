#include "serial_output.h"
#if CHAINPAD_SERIAL_BUFFERED
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace chimera {
namespace serialout {
namespace { Output shared; Output* formatting = nullptr; }
Output& output() { return shared; }
bool Output::begin(size_t budget, bool reportRejection) {
  if (building_ || formatting || !budget || budget > Capacity - count_) {
    if (reportRejection) { ++stats_.outputRejected; notice_ = true; }
    return false;
  }
  building_ = true; bad_ = false; reserved_ = budget; staged_ = 0;
  formatting = this;
  clearWriteError();
  return true;
}
void Output::formatFailure() { if (building_) { bad_ = true; setWriteError(); } }
size_t Output::write(const uint8_t* data, size_t size) {
  if (!building_ || bad_) return 0;
  if (size > reserved_ - staged_) { formatFailure(); return 0; }
  for (size_t i = 0; i < size; ++i) bytes_[(head_ + count_ + staged_ + i) % Capacity] = data[i];
  staged_ += size;
  return size;
}
bool Output::commit() {
  if (!building_) return false;
  if (bad_ || getWriteError()) {
    ++stats_.formatRejected; notice_ = true; cancel(); return false;
  }
  count_ += staged_;
  stats_.maxQueuedBytes = std::max(stats_.maxQueuedBytes, uint32_t(count_));
  cancel(); return true;
}
void Output::cancel() {
  if (formatting == this) formatting = nullptr;
  building_ = false; reserved_ = staged_ = 0; bad_ = false;
}
void formatFailure(Print& sink) {
  if (formatting && &sink == formatting) formatting->formatFailure();
}
void Output::pump(Print& sink, bool connected, bool paused) {
  if (paused) return;
  const uint32_t started = micros();
  if (connected && !connected_) {
    if (everConnected_) { boundary_ = true; boundaryOffset_ = 0; }
    everConnected_ = true;
  }
  connected_ = connected;
  // Never expose staged bytes, or change their ring position during a transaction.
  if (connected && !building_) {
    static constexpr char boundary[] = "\nSERIAL session resumed; retained suffix follows (USB delivery unknown)\n";
    const uint8_t* data = nullptr; size_t size = 0;
    if (boundary_) {
      data = reinterpret_cast<const uint8_t*>(boundary) + boundaryOffset_;
      size = std::min(PumpBytes, sizeof(boundary) - 1 - boundaryOffset_);
    } else {
      // Coalesce until prior data drains: repeated refusal must not consume all
      // recovered space and starve a larger diagnostic/trace reservation.
      if (notice_ && !count_ && begin(192, false)) {
        outputPrintf(*this, "SERIAL output loss: rejected=%lu format=%lu replies=%lu\n",
          static_cast<unsigned long>(stats_.outputRejected), static_cast<unsigned long>(stats_.formatRejected),
          static_cast<unsigned long>(stats_.commandReplyDrops));
        if (commit()) notice_ = false;
      }
      if (count_) {
        data = bytes_ + head_; size = std::min(PumpBytes, std::min(count_, Capacity - head_));
      }
    }
    if (size) {
      const size_t sent = std::min(size, sink.write(data, size)); // Exactly one sink write.
      if (!sent) ++stats_.txZero;
      else if (sent < size) ++stats_.txPartial;
      if (boundary_) {
        boundaryOffset_ += sent;
        if (boundaryOffset_ == sizeof(boundary) - 1) boundary_ = false;
      } else { head_ = (head_ + sent) % Capacity; count_ -= sent; }
    }
  }
  stats_.pumpMaxUs = std::max(stats_.pumpMaxUs, uint32_t(micros() - started));
}
void printStats(Print& out) {
  printStats(out, output().stats());
}
void printStats(Print& out, const Stats& s) {
  outputPrintf(out, "SERIAL txPartial=%lu txZero=%lu outputRejected=%lu formatRejected=%lu commandReplyDrops=%lu resultDeferred=%lu queuedBytes=%lu maxQueuedBytes=%lu pumpMaxUs=%lu\n",
    static_cast<unsigned long>(s.txPartial), static_cast<unsigned long>(s.txZero), static_cast<unsigned long>(s.outputRejected),
    static_cast<unsigned long>(s.formatRejected), static_cast<unsigned long>(s.commandReplyDrops), static_cast<unsigned long>(s.resultDeferred),
    static_cast<unsigned long>(s.queuedBytes), static_cast<unsigned long>(s.maxQueuedBytes), static_cast<unsigned long>(s.pumpMaxUs));
}
}
void outputPrintf(Print& out, const char* format, ...) {
  char text[512];
  va_list args; va_start(args, format);
  const int size = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (size < 0 || size >= int(sizeof(text))) {
    serialout::formatFailure(out); // A deliberate discard sink has no transaction.
    return;
  }
  out.write(reinterpret_cast<const uint8_t*>(text), size_t(size));
}
}
#endif
