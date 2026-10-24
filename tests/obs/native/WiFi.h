#pragma once
#include "Arduino.h"
#include "IPAddress.h"
#include <vector>
class WiFiClient {
public:
  std::vector<uint8_t> rx, tx;
  size_t offset = 0;
  bool open = true;
  bool connected() { return open; }
  int available() { return int(rx.size() - offset); }
  int read() { return available() ? rx[offset++] : -1; }
  int read(uint8_t* out, size_t n) {
    n = std::min(n, rx.size() - offset);
    if (n) memcpy(out, rx.data() + offset, n);
    offset += n; return int(n);
  }
  size_t write(const uint8_t* bytes, size_t n) { tx.insert(tx.end(), bytes, bytes + n); return n; }
};
class WiFiServer {};
using arduino_event_id_t = int;
using arduino_event_info_t = int;
constexpr int WIFI_STA=1, WIFI_AP=2, WL_CONNECTED=3, ARDUINO_EVENT_WIFI_STA_DISCONNECTED=5;
struct FakeWiFi {
  int mode = WIFI_STA, state = WL_CONNECTED;
  std::function<void(int,int)> disconnectEvent;
  int getMode() const { return mode; }
  int status() const { return state; }
  void onEvent(std::function<void(int,int)> fn, int) { disconnectEvent=fn; }
  void lose() { state=0; disconnectEvent(5,0); }
};
inline FakeWiFi WiFi;
