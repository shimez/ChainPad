#pragma once
#include <stdint.h>
#include <limits.h>

namespace chimera { namespace obs {
// Caller serializes this small state with a short critical section. No I/O,
// allocations or JSON under the lock. A claim is the send-start linearization
// point: after it the request is in-flight (Panic cannot undo external I/O).
struct Epoch {
  uint32_t settings = 0, wifi = 0, execution = 0, session = 0;
  bool operator==(const Epoch& b) const {
    return settings == b.settings && wifi == b.wifi && execution == b.execution && session == b.session;
  }
};
class Lifecycle {
  bool exhausted = false;
  void bump(uint32_t& value) { ready = false; if (value == UINT32_MAX) exhausted = true; else ++value; }
public:
  Epoch epoch;
  bool ready = false, allowed = false, enabled = false;
  uint32_t sequence = 0;
  void settings(bool on) { enabled = on; bump(epoch.settings); }
  void wifiLost() { bump(epoch.wifi); }
  void panic() { bump(epoch.execution); }
  void disconnected() { bump(epoch.session); }
  void permit(bool on) { if (allowed && !on) panic(); allowed = on; }
  bool current(const Epoch& e) const { return !exhausted && allowed && enabled && epoch == e; }
  bool publishReady(const Epoch& e) { ready = current(e); return ready; }
  bool claim(const Epoch& e) const { return ready && current(e); }
  uint32_t nextId() { return sequence == UINT32_MAX ? 0 : ++sequence; }
};
} }
