#include "hid_trace.h"
#if CHAINPAD_HID_TRACE
#include "serial_output.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
namespace chimera { namespace hidtrace {
namespace {
struct Entry {
  int64_t us;
  uint32_t id, epoch;
  int32_t value;
  uint16_t usage;
  uint8_t event, transport;
};
constexpr unsigned Capacity = 256;
Entry entries[Capacity];
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t counts[Count]{}, inputs[28]{}, statusesOk = 0, statusesError = 0;
uint32_t notifyTrue = 0, notifyFalse = 0, lost = 0, discarded = 0;
uint32_t usageCalls[256]{}, lastDownId[2]{}, pairs = 0;
int64_t lastDownUs[2]{}, pairTotal = 0, pairMax = 0;
unsigned used = 0, head = 0, high[2]{}, depth[2]{}, initial[2]{}, finalDepth[2]{};
uint8_t mode = 0, capturedMode = 0;
int64_t startUs = 0, stopUs = 0;
bool dumping = false;
unsigned dumpLine = 0;
uint32_t sequence = 0, reportContext[2]{}; // loop-task owned, independent of recording sessions
const char* names[] = {"input", "input_queued", "input_dropped", "input_consumed",
  "action_accepted", "action_rejected", "chain_rejected", "push", "pop", "discard",
  "fifo_overflow", "neutral", "notify_call", "notify_return", "status", "subscribe", "disconnect", "reset", "dispatch"};
}
uint32_t nextId() { return ++sequence; }
void context(uint8_t t, uint32_t id) { reportContext[t] = id; }
uint32_t context(uint8_t t) { return reportContext[t]; }
bool active() {
  portENTER_CRITICAL(&mux); const bool result = mode != 0; portEXIT_CRITICAL(&mux); return result;
}
void record(Event e, uint8_t t, uint16_t usage, uint32_t id, uint32_t epoch, int32_t value) {
  portENTER_CRITICAL(&mux);
  // Track boundary depths even while recording is stopped. No HID state is changed.
  if (t < 2) {
    if (e == Push || e == Pop) depth[t] = unsigned(value);
    if (e == Discard) depth[t] = 0;
  }
  if (!mode) { portEXIT_CRITICAL(&mux); return; }
  ++counts[e];
  if (e == Input && usage < 28) ++inputs[usage];
  if (e == Status) { if (value == 0) ++statusesOk; else ++statusesError; }
  if (e == NotifyReturn) { if (value) ++notifyTrue; else ++notifyFalse; }
  if (e == NotifyCall && t < 2) {
    if (usage < 256) ++usageCalls[usage];
    const int64_t now = esp_timer_get_time();
    if (usage && id && lastDownId[t] != id) { lastDownId[t] = id; lastDownUs[t] = now; }
    else if (!usage && id && lastDownId[t] && id == lastDownId[t] + 1) {
      const int64_t elapsed = now - lastDownUs[t];
      ++pairs; pairTotal += elapsed; if (elapsed > pairMax) pairMax = elapsed;
      lastDownId[t] = 0;
    }
  }
  if (t < 2 && (e == Discard || e == Neutral || e == Disconnect || e == Subscribe || e == Reset)) lastDownId[t] = 0;
  if (e == Discard) discarded += uint32_t(value);
  if (t < 2 && depth[t] > high[t]) high[t] = depth[t];
  if (mode == 2) {
    if (used == Capacity) { ++lost; head = (head + 1) % Capacity; }
    else ++used;
    entries[(head + used - 1) % Capacity] = {esp_timer_get_time(), id, epoch, value, usage, uint8_t(e), t};
  }
  portEXIT_CRITICAL(&mux);
}
bool command(int ch) {
  if (ch != '1' && ch != '2' && ch != '0' && ch != 'p') return false;
  portENTER_CRITICAL(&mux);
  if (ch == '1' || ch == '2') {
    mode = 0; dumping = false;
    memset(counts, 0, sizeof(counts)); memset(inputs, 0, sizeof(inputs));
    statusesOk = statusesError = notifyTrue = notifyFalse = lost = discarded = 0;
    memset(usageCalls, 0, sizeof(usageCalls)); memset(lastDownId, 0, sizeof(lastDownId));
    pairs = 0; pairTotal = pairMax = 0;
    head = used = 0;
    for (unsigned i = 0; i < 2; ++i) initial[i] = finalDepth[i] = high[i] = depth[i];
    startUs = esp_timer_get_time(); stopUs = 0; mode = capturedMode = ch == '1' ? 1 : 2;
  } else if (ch == '0' && mode) {
    mode = 0; stopUs = esp_timer_get_time();
    for (unsigned i = 0; i < 2; ++i) finalDepth[i] = depth[i];
  } else if (ch == 'p' && !mode) { dumpLine = 0; dumping = true; }
  portEXIT_CRITICAL(&mux);
  return true;
}
void dumpTick(Print& direct) {
  // command/dump run on loop only. With mode=0 callbacks cannot mutate this snapshot.
  if (!dumping) return;
  bool finished = false;
  const bool accepted = serialReport(direct, 384, [&](Print& out) {
  if (dumpLine == 0) {
    outputPrintf(out, "TRACE mode=%u start_us=%lld stop_us=%lld stored=%u lost=%lu bytes=%u status_ok=%lu status_error=%lu notify_true=%lu notify_false=%lu discarded_reports=%lu\n",
      capturedMode, startUs, stopUs, used, lost, unsigned(sizeof(entries)), statusesOk, statusesError, notifyTrue, notifyFalse, discarded);
  } else if (dumpLine <= Count) {
    unsigned i = dumpLine - 1; outputPrintf(out, "COUNT %s %lu\n", names[i], counts[i]);
  } else if (dumpLine <= Count + 28) {
    unsigned i = dumpLine - Count - 1; outputPrintf(out, "INPUT %u %lu\n", i, inputs[i]);
  } else if (dumpLine <= Count + 30) {
    unsigned i = dumpLine - Count - 29;
    outputPrintf(out, "FIFO transport=%u initial=%u final=%u high=%u\n", i, initial[i], finalDepth[i], high[i]);
  } else if (dumpLine == Count + 31) {
    outputPrintf(out, "PAIR notify_request_pairs=%lu mean_us=%lld max_us=%lld (not receiver hold time)\n",
      pairs, pairs ? pairTotal / pairs : 0, pairMax);
  } else if (dumpLine <= Count + 39) {
    const unsigned usages[] = {0, 233, 234, 226, 205, 181, 182, 183};
    const unsigned usage = usages[dumpLine - Count - 32];
    outputPrintf(out, "USAGE notify_requests usage=%u count=%lu\n", usage, usageCalls[usage]);
  } else if (dumpLine == Count + 40) {
    out.println("us,event,transport,usage,report_id,epoch,value");
  } else {
    unsigned i = dumpLine - Count - 41;
    if (i >= used) { finished = true; out.println("TRACE END"); return; }
    const auto& e = entries[(head + i) % Capacity];
    outputPrintf(out, "%lld,%s,%u,%u,%lu,%lu,%ld\n", e.us, names[e.event], e.transport, e.usage, e.id, e.epoch, e.value);
  }
  });
  if (accepted) {
    if (finished) dumping = false;
    else ++dumpLine; // Retry the same frozen line if output was not admitted.
  }
}
}}
#endif
