#pragma once
#include "model.h"
// Test driver for the same bounded transaction API used by the browser.
inline bool saveSource(const chimera::Config& source, String& error) {
  JsonDocument doc; String json;
  JsonDocument network; chimera::encodeNetwork(source, network);
  doc["schemaVersion"] = 2; doc["network"] = network; serializeJson(doc, json);
  auto token = chimera::beginConfigSave(json, error);
  if (!token) return false;
  for (uint8_t i = 0; i < chimera::INPUT_COUNT; ++i) {
    chimera::encodeChain(source.chains[i], i, doc); json = ""; serializeJson(doc, json);
    if (!chimera::stageConfigChain(token, i, json, error)) return false;
  }
  chimera::encodeRotation(source.encoderRotation, doc, true); json = ""; serializeJson(doc, json);
  if (!chimera::stageConfigRotation(token, json, error)) return false;
  return chimera::commitConfigSave(token, error);
}
