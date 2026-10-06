#include "diagnostics.h"
#include "model.h"
#include "rotation_runtime.h"
#include "engine.h"
#include "backends.h"
#include "inputs.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace chimera {
namespace {
int fileBytes(const char* path) {
  if (!LittleFS.exists(path)) return -1;
  auto file = LittleFS.open(path, "r");
  return file ? static_cast<int>(file.size()) : -2;
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
  out.printf("Rotation storage: Output=%u Settings=%u count=%u/%u state=%s\n",
    unsigned(sizeof(RotationOutput)), unsigned(sizeof(EncoderRotationSettings)), unsigned(config.encoderRotation.outputCount),
    unsigned(ROTATION_OUTPUT_CAPACITY), configStorageStateName());
  out.printf("Rotation runtime: bytes=%u active=%u range=%lu position=", unsigned(sizeof(RotationRuntime)),
    unsigned(rotationRuntime.active()), (unsigned long)config.encoderRotation.axis.rangeSteps);
  if (rotationRuntime.active()) out.printf("%lu", (unsigned long)rotationRuntime.position());
  else out.print("n/a");
  out.println(" (Phase C: no Rotation output sending)");
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
