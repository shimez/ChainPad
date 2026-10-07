#pragma once
#include <vector>
#include "IPAddress.h"
namespace fake { inline std::vector<uint8_t> osc; inline bool udpWritable = true; inline std::vector<std::vector<uint8_t>> packets; }
class WiFiUDP {
 public:
  bool beginPacket(IPAddress, uint16_t) { fake::osc.clear(); return true; }
  size_t write(const uint8_t* p, size_t size) { fake::osc.assign(p, p + size); return size; }
  bool endPacket() { if (!fake::udpWritable) return false; fake::packets.push_back(fake::osc); return true; }
};
