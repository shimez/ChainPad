#pragma once
#include "capabilities.h"
#if CHAINPAD_HAS_OBS
#include "obs_backend.h"
#include <ArduinoJson.h>

namespace chimera { namespace obssettings {
constexpr size_t MaxJsonBytes = 1024;
enum class State { Missing, Ready, Corrupt, UnsupportedVersion, IoError };
enum class Error { None, InvalidInput, TooLarge, NoMemory, PasswordRequired,
                   ReplaceRequired, Io, Verify, Rename };
const char* stateName(State state);
const char* errorName(Error error); // Fixed codes only: never includes input text.
bool equal(const ObsSettings& a, const ObsSettings& b);
// Failed decoding never modifies out. nullptr means no trustworthy password source.
Error decodeUpdate(const char* json, size_t size, const ObsSettings* previous, ObsSettings& out);
State decodeStored(const char* json, size_t size, ObsSettings& out, Error& error);
// Caller-owned bounded output; zero return means failure. Public form has no password.
size_t encodeStored(const ObsSettings& settings, char* out, size_t capacity);
size_t encodePublic(const ObsSettings& settings, char* out, size_t capacity);
} }
#endif
