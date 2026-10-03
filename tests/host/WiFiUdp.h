#pragma once
#include <vector>
#include "IPAddress.h"
namespace fake { inline std::vector<uint8_t> osc; }
class WiFiUDP {
 public:
  bool beginPacket(IPAddress, uint16_t) { fake::osc.clear(); return true; }
  size_t write(const uint8_t* p, size_t size) { fake::osc.assign(p, p + size); return size; }
  bool endPacket() { return true; }
};
