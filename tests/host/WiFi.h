#pragma once
#include "IPAddress.h"
constexpr int WL_CONNECTED = 3;
namespace fake { inline bool wifi = true; }
inline struct Wifi {
  int status() const { return fake::wifi ? WL_CONNECTED : 0; }
  int softAPgetStationNum() const { return 0; }
} WiFi;
