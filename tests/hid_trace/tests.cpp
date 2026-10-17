#include "hid_trace.h"
#include "esp_timer.h"
#include <cassert>
#include <iostream>
#include <thread>
using namespace chimera::hidtrace;
std::string dump() {
  assert(command('p')); Print out;
  for (unsigned i = 0; i < 400; ++i) dumpTick(out);
  return out.text;
}
void contains(const std::string& s, const char* expected) { assert(s.find(expected) != std::string::npos); }
int main() {
  // Boundaries include existing work; stop freezes counters while queue tracking continues.
  record(Push, 1, 233, 1, 7, 1);
  command('1'); assert(active());
  record(Input, 255, 0); record(ActionAccepted, 1, 233);
  record(Push, 1, 0, 2, 7, 2);
  traceClock = 100; record(NotifyCall, 1, 233, 1, 7);
  record(NotifyReturn, 1, 233, 1, 7, 1); record(Pop, 1, 233, 1, 7, 1);
  traceClock = 150; record(NotifyCall, 1, 0, 2, 7);
  record(NotifyReturn, 1, 0, 2, 7, 1); record(Pop, 1, 0, 2, 7, 0);
  record(Status, 1, 0, 12, 7, 0); record(Status, 1, 0, 12, 7, 8);
  command('0'); assert(!active()); record(Status, 1);
  auto s = dump(); contains(s, "stored=0 lost=0"); contains(s, "status_ok=1 status_error=1");
  contains(s, "initial=1 final=0 high=2"); contains(s, "notify_request_pairs=1 mean_us=50 max_us=50");
  // Detailed ring overwrites oldest records; counter totals remain complete.
  command('2'); command('p'); Print quiet; dumpTick(quiet); assert(quiet.text.empty());
  for (unsigned i = 0; i < 300; ++i) record(NotifyCall, 1, 233, i + 10, 8);
  record(Overflow, 1, 233, 0, 8, 64);
  record(Discard, 1, 233, 99, 8, 1);
  command('0'); s = dump(); contains(s, "stored=256 lost=46");
  contains(s, "COUNT fifo_overflow 1"); contains(s, "COUNT notify_call 300");
  contains(s, "discarded_reports=1"); contains(s, ",discard,1,233,99,8,1");
  // Callback and scanner producers cannot corrupt shared counters/ring.
  command('2');
  std::thread a([] { for (int i=0;i<1000;++i) record(Status,1,0,3,9,0); });
  std::thread b([] { for (int i=0;i<1000;++i) record(Input,255,24); });
  a.join(); b.join(); command('0'); s=dump();
  contains(s,"stored=256 lost=1744"); contains(s,"status_ok=1000"); contains(s,"INPUT 24 1000");
  assert(nextId() != nextId()); context(1,42); assert(context(1)==42);
  std::cout << "PASS HID trace: counter/event modes, frozen dump, FIFO boundaries, pair timing, ring loss vs overflow, concurrent producers\n";
}
