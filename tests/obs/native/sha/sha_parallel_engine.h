#pragma once
#include <cassert>
#define SHA1 0
inline void esp_sha(int, unsigned char*, size_t, unsigned char*) {
  assert(false && "Native frame tests do not exercise HTTP handshake SHA1");
}
