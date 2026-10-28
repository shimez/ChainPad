#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Shared schema, including builds without an OBS Worker. No networking dependency.
namespace chimera { namespace obs {
constexpr size_t TARGET_LIMIT = 192;
enum class Operation : uint8_t { SetCurrentProgramScene, StartStream, StopStream, StartRecord, StopRecord, SetInputMute, ToggleInputMute, SetSceneItemVisibility, GetSceneList, GetInputList, GetGroupList, GetSceneItemList, GetGroupSceneItemList, GetSceneItemEnabled };
enum class Visibility : uint8_t { Enable, Disable, Toggle };
constexpr uint64_t ITEM_ID_LIMIT = 9007199254740991ULL;
inline bool itemList(Operation op) { return op == Operation::GetSceneItemList || op == Operation::GetGroupSceneItemList; }
inline bool discovery(Operation op) { return op == Operation::GetSceneList || op == Operation::GetInputList || op == Operation::GetGroupList || itemList(op); }
inline const char* visibilityName(Visibility value) { return value == Visibility::Enable ? "enable" : value == Visibility::Disable ? "disable" : value == Visibility::Toggle ? "toggle" : nullptr; }
// Stored over the mutually exclusive OSC string field, without 64-bit alignment growth.
struct ItemSettings {
  uint8_t idBytes[8];
  bool group;
  Visibility visibility;
  uint64_t id() const { uint64_t value; memcpy(&value, idBytes, sizeof(value)); return value; }
  void id(uint64_t value) { memcpy(idBytes, &value, sizeof(value)); }
};
inline const char* operationName(Operation op) {
  switch (op) {
    case Operation::GetSceneList: return "getSceneList"; // Internal discovery, never an Action.
    case Operation::GetInputList: return "getInputList";
    case Operation::GetGroupList: return "getGroupList";
    case Operation::GetSceneItemList: return "getSceneItemList";
    case Operation::GetGroupSceneItemList: return "getGroupSceneItemList";
    case Operation::GetSceneItemEnabled: return "getSceneItemEnabled";
    case Operation::SetSceneItemVisibility: return "setSceneItemVisibility";
    case Operation::SetCurrentProgramScene: return "setCurrentProgramScene";
    case Operation::StartStream: return "startStream";
    case Operation::StopStream: return "stopStream";
    case Operation::StartRecord: return "startRecord";
    case Operation::StopRecord: return "stopRecord";
    case Operation::SetInputMute: return "setInputMute";
    case Operation::ToggleInputMute: return "toggleInputMute";
  }
  return nullptr;
}
inline const char* requestType(Operation op) {
  switch (op) {
    case Operation::GetSceneList: return "GetSceneList";
    case Operation::GetInputList: return "GetInputList";
    case Operation::GetGroupList: return "GetGroupList";
    case Operation::GetSceneItemList: return "GetSceneItemList";
    case Operation::GetGroupSceneItemList: return "GetGroupSceneItemList";
    case Operation::GetSceneItemEnabled: return "GetSceneItemEnabled";
    case Operation::SetSceneItemVisibility: return "SetSceneItemEnabled";
    case Operation::SetCurrentProgramScene: return "SetCurrentProgramScene";
    case Operation::StartStream: return "StartStream";
    case Operation::StopStream: return "StopStream";
    case Operation::StartRecord: return "StartRecord";
    case Operation::StopRecord: return "StopRecord";
    case Operation::SetInputMute: return "SetInputMute";
    case Operation::ToggleInputMute: return "ToggleInputMute";
  }
  return nullptr;
}
inline bool named(Operation op) { return op == Operation::SetCurrentProgramScene || op == Operation::SetInputMute || op == Operation::ToggleInputMute || op == Operation::SetSceneItemVisibility || op == Operation::GetSceneItemEnabled || itemList(op); }
inline bool parseOperation(const char* name, Operation& out) {
  if (!name) return false;
  for (unsigned i = 0; i <= unsigned(Operation::SetSceneItemVisibility); ++i) {
    const auto op = Operation(i);
    if (!strcmp(name, operationName(op))) { out = op; return true; }
  }
  return false;
}
inline bool validText(const char* s, size_t maxBytes, bool allowEmpty = false) {
  if (!s || (!allowEmpty && !*s)) return false;
  size_t length = 0;
  while (length <= maxBytes && s[length]) ++length;
  if (length > maxBytes) return false;
  for (size_t i = 0; i < length;) {
    uint8_t c = uint8_t(s[i++]);
    if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; continue; }
    unsigned count; uint32_t value, minimum;
    if (c >= 0xc2 && c <= 0xdf) { count = 1; value = c & 31; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { count = 2; value = c & 15; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { count = 3; value = c & 7; minimum = 0x10000; }
    else return false;
    if (count > length - i) return false;
    while (count--) { c = uint8_t(s[i++]); if ((c & 0xc0) != 0x80) return false; value = (value << 6) | (c & 63); }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}
struct Command {
  Operation operation = Operation::SetCurrentProgramScene;
  char target[TARGET_LIMIT + 1]{};
  bool muted = false;
  uint64_t itemId = 0;
  Visibility visibility = Visibility::Enable;
};
inline bool validCommand(const Command& c) {
  return operationName(c.operation) && (named(c.operation) ? validText(c.target, TARGET_LIMIT) : c.target[0] == 0) &&
    (c.operation == Operation::SetInputMute || !c.muted) && c.itemId <= ITEM_ID_LIMIT && visibilityName(c.visibility);
}
} }
