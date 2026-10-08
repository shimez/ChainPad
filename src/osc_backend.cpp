#include "backend_internal.h"
#include <WiFi.h>
#include <WiFiUdp.h>

namespace chimera {
SendResult oscDispatch(const Action& a) {
  // AP clients can also receive OSC, even when station Wi-Fi is unavailable.
  if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0) return SendResult::Unavailable;
  static WiFiUDP udp;
  // Padded address + padded type tags + largest (string) argument.
  uint8_t packet[((MAX_OSC_ADDRESS_BYTES + 4) & ~size_t(3)) + 4 + ((MAX_OSC_STRING_BYTES + 4) & ~size_t(3))]{};
  size_t offset = 0;
  auto string = [&](const char* s) {
    size_t length = strlen(s) + 1;
    memcpy(packet + offset, s, length); offset = (offset + length + 3) & ~size_t(3);
  };
  string(a.address);
  const char* tags[] = {",i", ",f", a.boolValue ? ",T" : ",F", ",s"};
  string(tags[unsigned(a.oscType)]);
  if (a.oscType == OscType::Int || a.oscType == OscType::Float) {
    uint32_t bits;
    if (a.oscType == OscType::Int) bits = static_cast<uint32_t>(a.intValue);
    else memcpy(&bits, &a.floatValue, sizeof(bits));
    for (int shift = 24; shift >= 0; shift -= 8) packet[offset++] = (bits >> shift) & 255;
  } else if (a.oscType == OscType::String) string(a.stringValue);
  IPAddress target; target.fromString(activeConfig().oscHost);
  if (!udp.beginPacket(target, activeConfig().oscPort)) return SendResult::Failed;
  if (udp.write(packet, offset) != offset) { udp.endPacket(); return SendResult::Failed; }
  return udp.endPacket() ? SendResult::Accepted : SendResult::Failed;
}
}
