#pragma once
#include <stdint.h>
#include <stddef.h>

namespace chimera { namespace obs {
constexpr size_t MESSAGE_LIMIT = 2048, SCENE_LIMIT = 192;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 3000, HANDSHAKE_TIMEOUT_MS = 5000;
enum class State : uint8_t { Disabled, Disconnected, Connecting, Authenticating, Ready, Error };
enum class Received { Invalid, Identify, Ready, Result, Ignored };
using Authenticate = bool (*)(const char*, const char*, const char*, char*);
bool validText(const char* text, size_t maxBytes, bool allowEmpty = false);
bool terminalClose(uint16_t code);
struct Protocol {
  State state = State::Disconnected;
  uint32_t pending = 0, sentAt = 0;
  bool success = false;
  int code = 0;
  char wire[1536]{};
  void connected();
  Received receive(const char*, size_t, const char*, Authenticate);
  bool request(const char*, uint32_t, uint32_t);
  bool expire(uint32_t) const;
};
struct Assembly {
  char bytes[MESSAGE_LIMIT + 1]{};
  size_t length = 0;
  bool active = false;
  uint32_t started = 0;
  void reset();
  bool append(const uint8_t*, size_t, bool, uint32_t);
  bool expired(uint32_t) const;
};
} }
