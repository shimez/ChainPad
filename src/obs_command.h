#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Shared schema, including builds without an OBS Worker. No networking dependency.
namespace chimera { namespace obs {
constexpr size_t TARGET_LIMIT = 192;
enum class Operation : uint8_t { SetCurrentProgramScene, StartStream, StopStream, StartRecord, StopRecord, SetInputMute, ToggleInputMute, SetSceneItemVisibility, TriggerMediaInputAction, SetInputVolume, GetSceneList, GetInputList, GetGroupList, GetSceneItemList, GetGroupSceneItemList, GetSceneItemEnabled, GetMediaInputList, GetInputVolume };
// Byte storage keeps the OSC-string union's byte alignment and Action size.
struct VolumeSettings {
  uint8_t valueBytes[sizeof(float)];
  bool relative;
  VolumeSettings(float value = 0, bool relativeValue = false) : relative(relativeValue) { memcpy(valueBytes, &value, sizeof(value)); }
  float value() const { float value; memcpy(&value, valueBytes, sizeof(value)); return value; }
};
inline bool validVolume(const VolumeSettings& v) {
  const float low=v.relative?-126.0f:-100.0f;
  const float high=v.relative?126.0f:26.0f;
  return v.value() >= low && v.value() <= high;
}
enum class Visibility : uint8_t { Enable, Disable, Toggle };
enum class MediaAction : uint8_t { Restart, Stop, Pause, Play, Next, Previous };
inline const char* mediaActionName(MediaAction action) {
  switch (action) {
    case MediaAction::Restart: return "restart";
    case MediaAction::Stop: return "stop";
    case MediaAction::Pause: return "pause";
    case MediaAction::Play: return "play";
    case MediaAction::Next: return "next";
    case MediaAction::Previous: return "previous";
  }
  return nullptr;
}
inline const char* mediaActionWire(MediaAction action) {
  switch (action) {
    case MediaAction::Restart: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART";
    case MediaAction::Stop: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_STOP";
    case MediaAction::Pause: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PAUSE";
    case MediaAction::Play: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PLAY";
    case MediaAction::Next: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_NEXT";
    case MediaAction::Previous: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PREVIOUS";
  }
  return nullptr;
}
inline bool parseMediaAction(const char* name, MediaAction& out) {
  if (!name) return false;
  for (unsigned i = 0; i <= unsigned(MediaAction::Previous); ++i) {
    const auto action = MediaAction(i);
    if (!strcmp(name, mediaActionName(action))) { out = action; return true; }
  }
  return false;
}
constexpr uint64_t ITEM_ID_LIMIT = 9007199254740991ULL;
inline bool itemList(Operation op) { return op == Operation::GetSceneItemList || op == Operation::GetGroupSceneItemList; }
inline bool discovery(Operation op) { return op == Operation::GetSceneList || op == Operation::GetInputList || op == Operation::GetMediaInputList || op == Operation::GetGroupList || itemList(op); }
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
    case Operation::SetInputVolume: return "setInputVolume";
    case Operation::GetInputVolume: return "getInputVolume";
    case Operation::GetInputList: return "getInputList";
    case Operation::GetMediaInputList: return "getMediaInputList";
    case Operation::TriggerMediaInputAction: return "triggerMediaInputAction";
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
    case Operation::SetInputVolume: return "SetInputVolume";
    case Operation::GetInputVolume: return "GetInputVolume";
    case Operation::GetInputList: return "GetInputList";
    case Operation::GetMediaInputList: return "GetInputList";
    case Operation::TriggerMediaInputAction: return "TriggerMediaInputAction";
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
inline bool named(Operation op) { return op == Operation::SetInputVolume || op == Operation::GetInputVolume || op == Operation::SetCurrentProgramScene || op == Operation::SetInputMute || op == Operation::ToggleInputMute || op == Operation::TriggerMediaInputAction || op == Operation::SetSceneItemVisibility || op == Operation::GetSceneItemEnabled || itemList(op); }
inline bool parseOperation(const char* name, Operation& out) {
  if (!name) return false;
  for (unsigned i = 0; i <= unsigned(Operation::SetInputVolume); ++i) {
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
  union { uint64_t itemId = 0; VolumeSettings volume; };
  Visibility visibility = Visibility::Enable;
  MediaAction mediaAction = MediaAction::Restart;
};
inline bool validCommand(const Command& c) {
  return operationName(c.operation) && (named(c.operation) ? validText(c.target, TARGET_LIMIT) : c.target[0] == 0) &&
    (c.operation == Operation::SetInputMute || !c.muted) &&
    ((c.operation == Operation::SetInputVolume || c.operation == Operation::GetInputVolume) ? validVolume(c.volume) : c.itemId <= ITEM_ID_LIMIT) && visibilityName(c.visibility) && mediaActionName(c.mediaAction);
}
} }
