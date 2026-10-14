#include "wifi_setup_flow.h"
#include <cassert>
#include <iostream>
using chimera::WifiSetupFlow;
using Change = WifiSetupFlow::Change;
int main() {
  WifiSetupFlow flow;
  assert(flow.begin(false, 100) == Change::StartPortal);
  assert(flow.tick(false, 30100) == Change::None);
  assert(flow.tick(false, 60100) == Change::None);
  assert(flow.begin(true, 100) == Change::None);
  assert(flow.tick(false, 30099) == Change::None);
  assert(flow.tick(false, 30100) == Change::StartPortal);
  assert(flow.tick(false, 60100) == Change::None);
  assert(flow.tick(true, 60101) == Change::None); // Late STA event cannot leave AP-only recovery.
  assert(flow.tick(true, 90000) == Change::None);
  assert(flow.begin(true, 90000) == Change::None); // Restart permits STA again.
  assert(flow.tick(true, 90001) == Change::None);
  assert(flow.tick(false, 100000) == Change::None);
  assert(flow.tick(false, 129999) == Change::None);
  assert(flow.tick(true, 130000) == Change::None); // Recover at deadline: no AP.
  assert(flow.tick(false, 140000) == Change::None);
  assert(flow.tick(false, 170000) == Change::StartPortal);
  assert(flow.tick(true, 170001) == Change::None);
  assert(flow.tick(false, 210000) == Change::None);
  assert(flow.begin(true, 0xfffffff0u) == Change::None);
  assert(flow.tick(false, uint32_t(0xfffffff0u + 29999u)) == Change::None);
  assert(flow.tick(false, uint32_t(0xfffffff0u + 30000u)) == Change::StartPortal);
  assert(flow.begin(true, 0) == Change::None);
  assert(flow.tick(true, 1000) == Change::None);
  assert(flow.tick(true, 60000) == Change::None);
  std::cout << "PASS Wi-Fi setup flow: no credentials, timeout, connected STA-only, sticky AP-only recovery, restart, later disconnect, timer wrap\n";
}
