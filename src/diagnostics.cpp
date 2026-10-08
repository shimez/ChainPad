#include "diagnostics.h"
#include "model.h"
#include "rotation_runtime.h"
#include "rotation_sender.h"
#include "engine.h"
#include "backends.h"
#include "inputs.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace chimera {
namespace {
struct MemorySample { const char* phase; uint32_t free, minimum, largest; };
struct AllocationFailure { const char* phase; const char* function; uint32_t size, caps, free, minimum, largest; };
MemorySample startup[16]{};
AllocationFailure failures[4]{};
unsigned startupCount = 0, failureCount = 0;
const char* currentPhase = "entry";
portMUX_TYPE memoryMux = portMUX_INITIALIZER_UNLOCKED;
void allocationFailed(size_t size, uint32_t caps, const char* function) {
  // The phase identifies the operation; function is the allocator API name,
  // not a captured backtrace. Keep this hook allocation-free and bounded.
  const uint32_t available = heap_caps_get_free_size(caps);
  const uint32_t minimum = heap_caps_get_minimum_free_size(caps);
  const uint32_t largest = heap_caps_get_largest_free_block(caps);
  portENTER_CRITICAL(&memoryMux);
  if (failureCount < 4) failures[failureCount] = {currentPhase, function, uint32_t(size), caps, available, minimum, largest};
  ++failureCount;
  portEXIT_CRITICAL(&memoryMux);
}
int fileBytes(const char* path) {
  if (!LittleFS.exists(path)) return -1;
  auto file = LittleFS.open(path, "r");
  return file ? static_cast<int>(file.size()) : -2;
}
}
void beginMemoryDiagnostics() {
  heap_caps_register_failed_alloc_callback(allocationFailed);
  memoryCheckpoint("entry");
}
void memoryPhase(const char* phase) {
  portENTER_CRITICAL(&memoryMux);
  currentPhase = phase;
  portEXIT_CRITICAL(&memoryMux);
}
void memoryCheckpoint(const char* phase) {
  memoryPhase(phase);
  if (startupCount < 16) {
    constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    startup[startupCount++] = {phase, uint32_t(heap_caps_get_free_size(caps)),
      uint32_t(heap_caps_get_minimum_free_size(caps)), uint32_t(heap_caps_get_largest_free_block(caps))};
  }
}
void printMemoryDiagnostics(Print& out) {
  out.printf("Memory records: startup=%u failures=%u recordBytes=%u\n", startupCount, failureCount,
    unsigned(sizeof(startup) + sizeof(failures)));
  for (unsigned i = 0; i < startupCount; ++i) {
    const auto& s = startup[i];
    out.printf("MEM %s free=%lu min=%lu largest=%lu\n", s.phase, s.free, s.minimum, s.largest);
  }
  for (unsigned i = 0; i < 4; ++i) {
    AllocationFailure f;
    portENTER_CRITICAL(&memoryMux);
    const bool valid = i < failureCount;
    if (valid) f = failures[i];
    portEXIT_CRITICAL(&memoryMux);
    if (valid) out.printf("ALLOC FAIL phase=%s api=%s size=%lu caps=0x%lx free=%lu min=%lu largest=%lu\n",
      f.phase, f.function, f.size, f.caps, f.free, f.minimum, f.largest);
  }
  for (const char* name : {"loopTask", "input-scan", "nimble_host", "wifi", "tiT", "esp_timer", "IDLE"}) {
    TaskHandle_t task = xTaskGetHandle(name);
    if (task) out.printf("STACK %s lowWaterBytes=%u\n", name, unsigned(uxTaskGetStackHighWaterMark(task)));
    else out.printf("STACK %s not found\n", name);
  }
}
void printDiagnostics(Print& out, const char* reason) {
  // Capture RAM before filesystem queries and printing allocate temporary buffers.
  multi_heap_info_t ram{}, psram{};
  heap_caps_get_info(&ram, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  heap_caps_get_info(&psram, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const auto stack = uxTaskGetStackHighWaterMark(nullptr); // ESP-IDF reports bytes.
  unsigned actions = 0, maxKey = 0, maxEvent = 0;
  for (unsigned i = 0; i < INPUT_COUNT; ++i) {
    actions += config.chains[i].count;
    if (config.chains[i].count > maxEvent) maxEvent = config.chains[i].count;
    if (i < 26 && !(i % 2)) {
      unsigned count = config.chains[i].count + config.chains[i + 1].count;
      if (count > maxKey) maxKey = count;
    }
  }
  out.printf("\n[diag] t=%lu reason=%s board=%s (bytes)\n", millis(), reason, HARDWARE_NAME);
  out.printf("RAM internal: free=%u minFree=%u largest=%u allocated=%u loopStackMin=%u\n",
    unsigned(ram.total_free_bytes), unsigned(ram.minimum_free_bytes), unsigned(ram.largest_free_block),
    unsigned(ram.total_allocated_bytes), unsigned(stack));
  out.printf("RAM PSRAM: free=%u minFree=%u largest=%u allocated=%u\n",
    unsigned(psram.total_free_bytes), unsigned(psram.minimum_free_bytes), unsigned(psram.largest_free_block), unsigned(psram.total_allocated_bytes));
  out.printf("Static sizes: Action=%u Chain=%u Config=%u Engine=%u; recordLimit=%u\n",
    unsigned(sizeof(Action)), unsigned(sizeof(Chain)), unsigned(sizeof(Config)), unsigned(sizeof(Engine)), unsigned(MAX_RECORD_BYTES));
  out.printf("Active Config address: %p\n", static_cast<void*>(&config));
  out.printf("Rotation storage: Output=%u Settings=%u count=%u/%u state=%s\n",
    unsigned(sizeof(RotationOutput)), unsigned(sizeof(EncoderRotationSettings)), unsigned(config.encoderRotation.outputCount),
    unsigned(ROTATION_OUTPUT_CAPACITY), configStorageStateName());
  out.printf("Rotation runtime: bytes=%u active=%u range=%lu position=", unsigned(sizeof(RotationRuntime)),
    unsigned(rotationRuntime.active()), (unsigned long)config.encoderRotation.axis.rangeSteps);
  if (rotationRuntime.active()) out.printf("%lu", (unsigned long)rotationRuntime.position());
  else out.print("n/a");
  out.println();
  out.printf("Rotation sender: bytes=%u pendingBytes=%u pending=%u budget=%u generation=%lu snapshot=%lu overwrite=%lu discard=%lu unavailable=%lu\n",
    unsigned(rotationSenderBytes()), unsigned(rotationPendingBytes()), rotationPendingCount(), ROTATION_SEND_BUDGET,
    rotationSendStats.generation, rotationSendStats.snapshot, rotationSendStats.overwritten,
    rotationSendStats.discarded, rotationSendStats.unavailable);
  out.printf("Rotation OSC/USB/BLE accepted=%lu/%lu/%lu failed=%lu/%lu/%lu\n",
    rotationSendStats.accepted[0], rotationSendStats.accepted[1], rotationSendStats.accepted[2],
    rotationSendStats.failed[0], rotationSendStats.failed[1], rotationSendStats.failed[2]);
  out.printf("Actions: total=%u/%u maxKey=%u/%u maxEvent=%u; running=%u rejected=%lu\n",
    actions, unsigned(MAX_TOTAL_ACTIONS), maxKey, unsigned(MAX_KEY_ACTIONS), maxEvent,
    unsigned(engine.activeCount()), engine.stats.rejectedChains);
  if (configStorageMounted()) {
    const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    out.printf("LittleFS: total=%u used=%u free=%u active=%d pending=%d\n",
      unsigned(total), unsigned(used), unsigned(total >= used ? total - used : 0),
      fileBytes("/config.records"), fileBytes("/pending.records"));
  } else out.println("LittleFS: unmounted (not mounted/formatted by diagnostics)");
  const auto status = backendStatus();
  out.printf("Counters: transportRetries=%lu transportOverflows=%lu inputOverflows=%lu\n",
    status.retries, status.overflows, inputOverflows());
}
}
