#pragma once
#include <stdint.h>

namespace chimera {
// Nonblocking fallback policy, using the credentials selected at boot.
class WifiSetupFlow {
 public:
  enum class Change { None, StartPortal };
  static constexpr uint32_t CONNECTION_TIMEOUT_MS = 30000;
  Change begin(bool credentials, uint32_t now) {
    credentials_ = credentials; connected_ = false;
    disconnectedAt_ = now; portal_ = !credentials;
    return portal_ ? Change::StartPortal : Change::None;
  }
  Change tick(bool connected, uint32_t now) {
    // AP-only recovery is sticky until restart; do not resume STA scanning.
    if (!credentials_ || portal_) return Change::None;
    if (connected) {
      connected_ = true;
    } else {
      if (connected_) { connected_ = false; disconnectedAt_ = now; }
      if (!portal_ && uint32_t(now - disconnectedAt_) >= CONNECTION_TIMEOUT_MS) {
        portal_ = true; return Change::StartPortal;
      }
    }
    return Change::None;
  }
 private:
  bool credentials_ = false, connected_ = false, portal_ = false;
  uint32_t disconnectedAt_ = 0;
};
}
