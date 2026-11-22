#include "save_mem.h"
#if CHAINPAD_SAVE_MEM
#include "serial_output.h"
#include <esp_heap_caps.h>
#if CHAINPAD_SAVE_RESERVE
#include <esp_memory_utils.h>
// An ordinary BSS object just like Engine; no early heap allocation.
alignas(4) static volatile uint8_t callChainReserve[7936];
#endif

namespace chimera::savemem {
namespace {
// All access is on loopTask. Preserve both minima even if from different records.
struct Row { uint32_t free, largest; uint16_t hits; uint8_t freeId, largestId; };
Row rows[Count]{};
bool armed = false;
constexpr const char* names[] = {"stage-decoded","stage-encoded","file-written","rotation-encoded",
                                 "validate-chain","apply-chain","validate-rotation","apply-rotation"};
}
void arm() {
  for (auto& row : rows) row = {};
  armed = true;
}
void sample(Point point, unsigned id) {
  if (!armed) return;
  multi_heap_info_t h{};
  heap_caps_get_info(&h, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  auto& r = rows[point];
  if (!r.hits || h.total_free_bytes < r.free) { r.free = h.total_free_bytes; r.freeId = id; }
  if (!r.hits || h.largest_free_block < r.largest) { r.largest = h.largest_free_block; r.largestId = id; }
  if (r.hits != UINT16_MAX) ++r.hits;
}
void print(Print& out) {
  armed = false;
#if CHAINPAD_SAVE_RESERVE
  outputPrintf(out, "RESERVE bytes=%u internal=%u\n", unsigned(sizeof(callChainReserve)),
               unsigned(esp_ptr_internal(const_cast<const uint8_t*>(callChainReserve))));
#endif
  for (unsigned i = 0; i < Count; ++i) {
    const auto& r = rows[i];
    outputPrintf(out, "SAVE %s n=%u free=%lu id=%u largest=%lu id=%u\n",
                 names[i], unsigned(r.hits), (unsigned long)r.free, unsigned(r.freeId),
                 (unsigned long)r.largest, unsigned(r.largestId));
  }
}
}
#endif
