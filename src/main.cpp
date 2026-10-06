#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "model.h"
#include "json_wire.h"
#include "rotation_runtime.h"
#if CHAINPAD_HAS_USB
#include <USBCDC.h>
#endif
#include "engine.h"
#include "inputs.h"
#include "backends.h"
#include "diagnostics.h"
#include "web_assets.h"

using namespace chimera;
namespace {
WebServer web(80);
DNSServer portalDns;
bool portalReady = false;
bool apRequest() { return web.client().localIP() == WiFi.softAPIP(); }
void redirectToSetup() {
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/?setup=wifi");
  web.send(302, "text/plain", "Open ChainPad Wi-Fi setup");
}
#if CHAINPAD_HAS_USB
USBCDC console; // Explicit CDC: USB is started once, after setting descriptors.
#else
auto& console = Serial; // C3/C6/C5: USB Serial/JTAG console only.
#endif
String bootMessage;
bool diagnosticsEnabled = false;
uint32_t lastDiagnostics = 0;
void diagnosticCheckpoint(const char* reason) {
  if (diagnosticsEnabled) printDiagnostics(console, reason);
}
bool inputReady = false;
uint32_t restartAt = 0, lastActivity = 0;
char bootSsid[33]{}, bootPassword[65]{};
void jsonResponse(int status, JsonDocument& doc) {
  String body; serializeSettingsJson(doc, body);
  web.sendHeader("Cache-Control", "no-store");
  web.send(status, "application/json; charset=utf-8", body);
}
void result(int status, const String& message) {
  JsonDocument doc; doc["ok"] = status < 400; doc["message"] = message; jsonResponse(status, doc);
}
void panic() { engine.cancelAll(); backendsPanic(); discardInputs(); }
void setupWeb() {
  web.on("/", HTTP_GET, [] {
    if (apRequest() && (!web.hasArg("setup") || web.hostHeader() != WiFi.softAPIP().toString())) {
      redirectToSetup(); return;
    }
    web.sendHeader("Cache-Control", "no-store");
    web.send_P(200, "text/html; charset=utf-8", apRequest() || web.arg("setup") == "wifi" ? WIFI_UI : WEB_UI);
  });
  web.on("/configurator", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "no-store");
    web.send_P(200, "text/html; charset=utf-8", WEB_UI);
  });
  web.on("/api/wifi", HTTP_GET, [] {
    JsonDocument doc; doc["ssid"] = config.ssid; doc["password"] = config.password;
    jsonResponse(200, doc);
  });
  web.on("/api/wifi", HTTP_PUT, [] {
    diagnosticCheckpoint("wifi-save-before");
    String error;
    if (!saveWifiConfig(web.arg("plain"), error)) { diagnosticCheckpoint("wifi-save-failed"); result(400, error); return; }
    diagnosticCheckpoint("wifi-save-after");
    panic(); restartAt = millis() + 750;
    result(200, "Saved. Restarting.");
  });
  web.on("/api/capabilities", HTTP_GET, [] {
    JsonDocument doc; encodeCapabilities(doc); jsonResponse(200, doc);
  });
  web.on("/api/config", HTTP_GET, [] {
    // Stream one record at a time; never construct the full JSON in device RAM.
    web.sendHeader("Cache-Control", "no-store");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "application/json; charset=utf-8", "");
    JsonDocument doc; String part;
    encodeNetwork(config, doc); serializeSettingsJson(doc, part);
    web.sendContent("{\"schemaVersion\":2,\"network\":"); web.sendContent(part);
    web.sendContent(",\"chains\":[");
    for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
      encodeChain(config.chains[i], i, doc); part = ""; serializeSettingsJson(doc, part);
      if (i) web.sendContent(",");
      web.sendContent(part);
    }
    web.sendContent("],\"encoderRotation\":");
    encodeRotation(config.encoderRotation, doc, true); part = ""; serializeSettingsJson(doc, part); web.sendContent(part);
    web.sendContent("}"); web.sendContent("");
    diagnosticCheckpoint("config-get-after");
  });
  web.on("/api/config/begin", HTTP_POST, [] {
    String error; auto token = beginConfigSave(web.arg("plain"), error);
    diagnosticCheckpoint(token ? "save-begin" : "save-begin-failed");
    if (!token) { result(400, error); return; }
    JsonDocument doc; doc["token"] = token; jsonResponse(200, doc);
  });
  web.on("/api/config/chain", HTTP_PUT, [] {
    int id = web.arg("input").toInt();
    String error;
    if (!web.hasArg("input") || id < 0 || id >= INPUT_COUNT ||
         !stageConfigChain(web.arg("token").toInt(), id, web.arg("plain"), error)) { diagnosticCheckpoint("save-stage-failed"); result(400, error); return; }
    diagnosticCheckpoint("save-stage");
    result(200, "Staged");
  });
  web.on("/api/config/rotation", HTTP_PUT, [] {
    String error;
    diagnosticCheckpoint("rotation-stage-before");
    if (!stageConfigRotation(web.arg("token").toInt(), web.arg("plain"), error)) { result(400, error); return; }
    diagnosticCheckpoint("rotation-stage-after");
    result(200, "Rotation staged");
  });
  web.on("/api/config/commit", HTTP_POST, [] {
    diagnosticCheckpoint("save-commit-before");
    String error;
    if (!commitConfigSave(web.arg("token").toInt(), error)) { if (!configOutputsAllowed()) panic(); diagnosticCheckpoint("save-commit-failed"); result(400, error); return; }
    diagnosticCheckpoint("save-commit-after");
    panic();
    bootMessage = "v2 settings saved and applied";
    result(200, "Saved and applied. Wi-Fi credential changes require Restart.");
  });
  web.on("/api/status", HTTP_GET, [] {
    JsonDocument doc;
    auto s = backendStatus();
    doc["name"] = "ChainPad"; doc["version"] = "0.5.0-chimera";
    doc["hardware"] = HARDWARE_NAME;
    doc["usbMidiSupported"] = HAS_USB_MIDI; doc["usbKeyboardSupported"] = HAS_USB_KEYBOARD;
    doc["keyCount"] = 12; doc["encoder"] = true; doc["encoderPush"] = true; doc["led"] = true;
    doc["wifi"] = s.wifi; doc["ip"] = WiFi.localIP().toString(); doc["apIp"] = WiFi.softAPIP().toString();
    doc["captivePortal"] = portalReady;
    doc["usbMidi"] = s.usbMidi; doc["usbKeyboard"] = s.usbKeyboard;
    doc["bleMidi"] = s.bleMidi; doc["bleKeyboard"] = s.bleKeyboard;
    doc["events"] = engine.stats.events; doc["accepted"] = engine.stats.accepted;
    doc["skipped"] = engine.stats.skipped;
    doc["runningChains"] = engine.activeCount(); doc["rejectedChains"] = engine.stats.rejectedChains;
    doc["cancelledChains"] = engine.stats.cancelledChains;
    doc["lastInput"] = inputName(engine.stats.lastInput);
    doc["lastAction"] = engine.stats.lastAction + 1;
    const char* results[] = {"accepted", "unavailable", "busy", "failed"};
    doc["lastResult"] = results[unsigned(engine.stats.lastResult)];
    doc["transportRetries"] = s.retries; doc["transportOverflows"] = s.overflows;
    doc["inputOverflows"] = inputOverflows(); doc["pressedMask"] = pressedInputs();
    doc["inputsReady"] = inputReady; doc["bootMessage"] = bootMessage;
    doc["storageState"] = configStorageStateName(); doc["configOutputsAllowed"] = configOutputsAllowed();
    auto rotation = doc["encoderRotation"].to<JsonObject>();
    rotation["mode"] = config.encoderRotation.mode == RotationMode::ActionChain ? "actionChain" : "rotationValue";
    rotation["rangeSteps"] = config.encoderRotation.axis.rangeSteps;
    rotation["runtimeActive"] = rotationRuntime.active();
    if (rotationRuntime.active()) rotation["currentPosition"] = rotationRuntime.position();
    else rotation["currentPosition"] = nullptr;
    rotation["outputSendingSupported"] = false;
    doc["restartRequired"] = strcmp(config.ssid, bootSsid) != 0 || strcmp(config.password, bootPassword) != 0;
    doc["freeHeap"] = ESP.getFreeHeap(); doc["uptimeMs"] = millis();
    doc["largestFreeBlock"] = ESP.getMaxAllocHeap(); doc["minFreeHeap"] = ESP.getMinFreeHeap();
    jsonResponse(200, doc);
  });
  web.on("/api/trigger", HTTP_POST, [] {
    if (!configOutputsAllowed()) { result(409, "Settings unavailable; outputs disabled"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, web.arg("plain")) || !doc["input"].is<unsigned>() || doc["input"].as<unsigned>() >= INPUT_COUNT) {
      result(400, "input must be 0..27"); return;
    }
    if (doc["input"].as<unsigned>() >= 26 && config.encoderRotation.mode == RotationMode::RotationValue) {
      result(409, "CW/CCW Action Chains are inactive in Rotation Value mode; API does not change Position"); return;
    }
    if (!engine.trigger({doc["input"].as<uint8_t>(), millis()})) { result(409, "Wait scheduler full (32 running chains)"); return; }
    lastActivity = millis();
    result(200, "Saved chain dispatched; see status for transport results.");
  });
  web.on("/api/panic", HTTP_POST, [] { panic(); result(200, "実行中のChainと未処理の入力・出力を破棄し、HID解除を要求しました。MIDIメッセージは送信しません。"); });
  web.on("/api/restart", HTTP_POST, [] { panic(); restartAt = millis() + 750; result(200, "Restarting"); });
  // Android /generate_204, Apple /hotspot-detect.html, Windows
  // /connecttest.txt (and other HTTP probes) all land on the same portal.
  web.onNotFound([] {
    if (apRequest() && web.method() == HTTP_GET && !web.uri().startsWith("/api/")) {
      redirectToSetup(); return;
    }
    result(404, "Not found");
  });
  web.begin();
}
}
void setup() {
  console.begin(115200);
  console.setTxTimeoutMs(0);
  loadConfig(bootMessage);
  memcpy(bootSsid, config.ssid, sizeof(bootSsid)); memcpy(bootPassword, config.password, sizeof(bootPassword));
  backendsBegin();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP("ChainPad-Setup", "chimera-pad")) {
    portalDns.setTTL(0);
    portalReady = portalDns.start(53, "*", WiFi.softAPIP());
    if (!portalReady) bootMessage += " / Captive DNS failed";
  } else bootMessage += " / Setup AP failed";
  WiFi.setAutoReconnect(true);
  if (config.ssid[0]) WiFi.begin(config.ssid, config.password);
  MDNS.begin("chainpad"); MDNS.addService("http", "tcp", 80);
  setupWeb();
  inputReady = inputsBegin();
   console.println("ChainPad / Project Chimera Phase 2"); console.println(bootMessage);
  console.println("Setup: ChainPad-Setup / http://192.168.4.1");
  console.println("Diagnostics: m=snapshot, d=toggle 5s + save logging, ?=help (115200 baud)");
}
void loop() {
  static uint32_t observedOverflows = 0;
  web.handleClient();
  // At most one command per loop, so pasted input cannot monopolize execution.
  if (console.available() > 0) {
    const int command = console.read();
    if (command == 'm') printDiagnostics(console, "manual");
    else if (command == 'd') {
      diagnosticsEnabled = !diagnosticsEnabled; lastDiagnostics = millis();
      console.println(diagnosticsEnabled ? "Diagnostics ON" : "Diagnostics OFF");
      if (diagnosticsEnabled) printDiagnostics(console, "enabled");
    } else if (command == '?') console.println("m=snapshot; d=toggle 5s + save logging (default OFF); bytes; file -1=absent -2=open failed");
  }
  uint32_t now = millis();
  backendsTick(now);
  uint32_t overflows = inputOverflows();
  if (overflows != observedOverflows) {
    observedOverflows = overflows; panic(); // Cancel pending work and release HID; no generated MIDI.
  }
  if (!restartAt) {
    if (configOutputsAllowed()) engine.tick(now);
    InputEvent event;
    for (unsigned budget = 0; budget < 16 && nextInput(event); ++budget) {
      if (configOutputsAllowed()) engine.trigger(event);
      lastActivity = now;
    }
  } else if (static_cast<int32_t>(now - restartAt) >= 0) ESP.restart();
  bool led = WiFi.status() == WL_CONNECTED ? now - lastActivity > 50 : now % 1000 < 500;
  setStatusLed(inputReady && led);
  if (diagnosticsEnabled && now - lastDiagnostics >= 5000) {
    lastDiagnostics = now; printDiagnostics(console, "periodic");
  }
  delay(1);
}
