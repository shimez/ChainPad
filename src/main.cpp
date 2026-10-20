#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "wifi_setup_flow.h"
#include "model.h"
#include "json_wire.h"
#include "rotation_runtime.h"
#include "rotation_sender.h"
#if CHAINPAD_HAS_USB
#include <USBCDC.h>
#endif
#include "engine.h"
#include "inputs.h"
#include "backends.h"
#include "diagnostics.h"
#include "factory_reset.h"
#include "web_assets.h"

#include "hid_trace.h"
using namespace chimera;
namespace {
WebServer web(80);
DNSServer portalDns;
bool portalReady = false;
bool setupApReady = false;
chimera::WifiSetupFlow wifiSetupFlow;
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
bool maintenanceRecovery = false;
uint32_t restartAt = 0, lastActivity = 0;
char bootSsid[33]{}, bootPassword[65]{};
void jsonResponse(int status, JsonDocument& doc) {
  String body;
  if (!checkedSettingsJson(doc, body)) {
    // No JSON allocation is needed for the fallback body. Preserve errors.
    web.send(status >= 400 ? status : 500, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"JSON generation failed\"}");
    return;
  }
  web.sendHeader("Cache-Control", "no-store");
  web.send(status, "application/json; charset=utf-8", body);
}
void result(int status, const String& message) {
  JsonDocument doc; doc["ok"] = status < 400; doc["message"] = message; jsonResponse(status, doc);
}
void panic() { engine.cancelAll(); backendsPanic(); discardInputs(); }
bool resetGuard() {
  if (!factoryResetBlocked()) return false;
  result(409, "Factory Reset pending or failed; settings and outputs are locked"); return true;
}
void setupWeb() {
  web.on("/api/factory-reset", HTTP_GET, [] {
    JsonDocument doc; doc["state"] = factoryResetStatus(); jsonResponse(200, doc);
  });
  web.on("/api/factory-reset", HTTP_POST, [] {
    JsonDocument doc;
    if (web.arg("plain").length() > 128 || deserializeJson(doc, web.arg("plain")) || doc["confirm"] != "factory-reset") {
      result(400, "Explicit Factory Reset confirmation required"); return;
    }
    const bool alreadyAccepted = strcmp(factoryResetStatus(), "accepted") == 0;
    if (!requestFactoryReset()) { result(500, "Could not persist reset request; use Web Installer if retry fails"); return; }
    panic();
    result(202, "Factory Reset accepted; erasure will run after restart. Reconnect to ChainPad-Setup.");
    if (!alreadyAccepted) restartAt = millis() + 750;
  });
#ifdef CHAINPAD_ROTATION_TEST
  web.on("/api/rotation-test/hold", HTTP_POST, [] {
    rotationTestHeld = web.arg("enabled") == "1";
    result(200, rotationTestHeld ? "Rotation test hold enabled" : "Rotation test hold disabled");
  });
#endif
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
    if (configCommittedApplyFailed()) { result(503, "Stored settings require reload. Outputs disabled; Restart required."); return; }
    JsonDocument doc; doc["ssid"] = activeConfig().ssid; doc["password"] = activeConfig().password;
    jsonResponse(200, doc);
  });
  web.on("/api/wifi", HTTP_PUT, [] {
    if (resetGuard()) return;
    diagnosticCheckpoint("wifi-save-before");
    String error;
    if (!saveWifiConfig(web.arg("plain"), error)) { if (!configOutputsAllowed()) panic(); diagnosticCheckpoint("wifi-save-failed"); result(400, error); return; }
    diagnosticCheckpoint("wifi-save-after");
    panic(); restartAt = millis() + 750;
    result(200, "Saved. Restarting.");
  });
  web.on("/api/capabilities", HTTP_GET, [] {
    JsonDocument doc; encodeCapabilities(doc); jsonResponse(200, doc);
  });
  web.on("/api/config", HTTP_GET, [] {
    if (resetGuard()) return;
    if (configCommittedApplyFailed()) { result(503, "Stored settings require reload. Outputs disabled; Restart required."); return; }
    // Same checked encoder in both passes; retain only one record at a time.
    JsonDocument doc; String part;
    auto record = [&](uint8_t index) {
      bool ok;
      if (!index) ok = encodeNetwork(activeConfig(), doc);
      else if (index <= INPUT_COUNT) ok = encodeChain(activeConfig().chains[index - 1], index - 1, doc);
      else ok = encodeRotation(activeConfig().encoderRotation, doc, true);
      return ok && checkedSettingsJson(doc, part);
    };
    for (uint8_t i = 0; i < INPUT_COUNT + 2; ++i) {
      if (!record(i)) { result(500, "Config encode or serialization failed"); return; }
    }
    web.sendHeader("Cache-Control", "no-store");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "application/json; charset=utf-8", "");
    // Stop the client on any second-pass failure. WebServer's automatic final
    // chunk then has no connected socket; do not close the JSON or send an error.
    if (!record(0)) { web.client().stop(); return; }
    web.sendContent("{\"schemaVersion\":2,\"network\":"); web.sendContent(part);
    web.sendContent(",\"chains\":[");
    for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
      if (!record(i + 1)) { web.client().stop(); return; }
      if (i) web.sendContent(",");
      web.sendContent(part);
    }
    if (!record(INPUT_COUNT + 1)) { web.client().stop(); return; }
    web.sendContent("],\"encoderRotation\":"); web.sendContent(part);
    web.sendContent("}"); web.sendContent("");
    diagnosticCheckpoint("config-get-after");
  });
  web.on("/api/config/begin", HTTP_POST, [] {
    if (resetGuard()) return;
    String error; auto token = beginConfigSave(web.arg("plain"), error);
    diagnosticCheckpoint(token ? "save-begin" : "save-begin-failed");
    if (!token) { result(400, error); return; }
    JsonDocument doc; doc["token"] = token; jsonResponse(200, doc);
  });
  web.on("/api/config/chain", HTTP_PUT, [] {
    if (resetGuard()) return;
    int id = web.arg("input").toInt();
    String error;
    if (!web.hasArg("input") || id < 0 || id >= INPUT_COUNT ||
         !stageConfigChain(web.arg("token").toInt(), id, web.arg("plain"), error)) { diagnosticCheckpoint("save-stage-failed"); result(400, error); return; }
    diagnosticCheckpoint("save-stage");
    result(200, "Staged");
  });
  web.on("/api/config/rotation", HTTP_PUT, [] {
    if (resetGuard()) return;
    String error;
    diagnosticCheckpoint("rotation-stage-before");
    if (!stageConfigRotation(web.arg("token").toInt(), web.arg("plain"), error)) { result(400, error); return; }
    diagnosticCheckpoint("rotation-stage-after");
    result(200, "Rotation staged");
  });
  web.on("/api/config/commit", HTTP_POST, [] {
    if (resetGuard()) return;
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
    doc["name"] = "ChainPad"; doc["version"] = "0.5.3";
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
    doc["storageState"] = configStorageStateName(); doc["configOutputsAllowed"] = configOutputsAllowed() && !factoryResetBlocked();
    auto rotation = doc["encoderRotation"].to<JsonObject>();
    rotation["mode"] = activeConfig().encoderRotation.mode == RotationMode::ActionChain ? "actionChain" : "rotationValue";
    rotation["rangeSteps"] = activeConfig().encoderRotation.axis.rangeSteps;
    rotation["runtimeActive"] = rotationRuntime.active();
    if (rotationRuntime.active()) rotation["currentPosition"] = rotationRuntime.position();
    else rotation["currentPosition"] = nullptr;
    rotation["outputSendingSupported"] = true;
    rotation["generation"] = rotationSendStats.generation;
    rotation["snapshot"] = rotationSendStats.snapshot;
    rotation["pending"] = rotationPendingCount();
    rotation["pendingBytes"] = rotationPendingBytes();
    rotation["senderBytes"] = rotationSenderBytes();
    rotation["overwritten"] = rotationSendStats.overwritten;
    rotation["discarded"] = rotationSendStats.discarded;
    rotation["unavailable"] = rotationSendStats.unavailable;
    auto accepted = rotation["accepted"].to<JsonArray>();
    auto failed = rotation["failed"].to<JsonArray>();
    for (unsigned i = 0; i < ROTATION_ROUTES; ++i) {
      accepted.add(rotationSendStats.accepted[i]); failed.add(rotationSendStats.failed[i]);
    }
    doc["restartRequired"] = strcmp(activeConfig().ssid, bootSsid) != 0 || strcmp(activeConfig().password, bootPassword) != 0;
    doc["freeHeap"] = ESP.getFreeHeap(); doc["uptimeMs"] = millis();
    doc["largestFreeBlock"] = ESP.getMaxAllocHeap(); doc["minFreeHeap"] = ESP.getMinFreeHeap();
    jsonResponse(200, doc);
  });
  web.on("/api/trigger", HTTP_POST, [] {
    if (resetGuard()) return;
    if (!configOutputsAllowed()) { result(409, "Settings unavailable; outputs disabled"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, web.arg("plain")) || !doc["input"].is<unsigned>() || doc["input"].as<unsigned>() >= INPUT_COUNT) {
      result(400, "input must be 0..27"); return;
    }
    if (doc["input"].as<unsigned>() >= 26 && activeConfig().encoderRotation.mode == RotationMode::RotationValue) {
      result(409, "CW/CCW Action Chains are inactive in Rotation Value mode; API does not change Position"); return;
    }
    if (!engine.trigger({doc["input"].as<uint8_t>(), millis()})) { result(409, "Chain scheduler full (32 running chains)"); return; }
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
void updateSetupPortal(WifiSetupFlow::Change change) {
  if (change == WifiSetupFlow::Change::StartPortal) {
    memoryPhase("wifi/softAP");
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false); // Stop STA without erasing credentials.
    if (WiFi.mode(WIFI_AP) && WiFi.softAP("ChainPad-Setup", "chimera-pad")) {
      setupApReady = true;
      portalDns.setTTL(0);
      portalReady = portalDns.start(53, "*", WiFi.softAPIP());
      if (!portalReady) bootMessage += " / Captive DNS failed";
    } else bootMessage += " / Setup AP failed";
    memoryCheckpoint("wifi/AP-DNS-ready");
  }
}
void setup() {
  beginMemoryDiagnostics();
  console.begin(115200);
  console.setTxTimeoutMs(0);
  const auto reset = factoryResetBoot();
  if (reset == ResetBoot::Restart) { ESP.restart(); return; }
  maintenanceRecovery = reset == ResetBoot::Failed;
  memoryPhase("config/load");
  if (maintenanceRecovery) bootMessage = "Factory Reset failed/stopped; retry explicitly or use Web Installer. Outputs disabled.";
  else loadConfig(bootMessage);
  auto& config = activeConfig();
  memoryCheckpoint("config/loaded");
  memcpy(bootSsid, config.ssid, sizeof(bootSsid)); memcpy(bootPassword, config.password, sizeof(bootPassword));
  if (!maintenanceRecovery) backendsBegin();
  memoryCheckpoint("backends/ready");
  WiFi.persistent(false);
  memoryPhase("wifi/mode");
  WiFi.mode(WIFI_STA);
  memoryCheckpoint("wifi/mode-ready");
  WiFi.setAutoReconnect(true);
  if (config.ssid[0]) WiFi.begin(config.ssid, config.password);
  updateSetupPortal(wifiSetupFlow.begin(config.ssid[0] != 0, millis()));
  memoryCheckpoint("wifi/STA-requested");
  MDNS.begin("chainpad"); MDNS.addService("http", "tcp", 80);
  memoryCheckpoint("mdns/ready");
  memoryPhase("web/routes");
  setupWeb();
  memoryCheckpoint("web/ready");
  memoryPhase("inputs/begin");
  if (!maintenanceRecovery) inputReady = inputsBegin();
  memoryCheckpoint(inputReady ? "inputs/ready" : "inputs/FAILED");
  memoryPhase("loop");
   console.println("ChainPad / Project Chimera Phase 2"); console.println(bootMessage);
  console.println("Setup AP: when unconfigured or disconnected for 30s / http://192.168.4.1");
  console.println("Diagnostics: m=snapshot, d=toggle 5s + save logging, ?=help (115200 baud)");
#if CHAINPAD_HID_TRACE
  console.println("HID TRACE: 1=start counters, 2=start events, 0=stop, p=dump (controls are silent)");
#endif
}
void loop() {
  static uint32_t observedOverflows = 0;
  web.handleClient();
  // At most one command per loop, so pasted input cannot monopolize execution.
  if (console.available() > 0) {
    const int command = console.read();
#if CHAINPAD_HID_TRACE
    if (hidtrace::command(command) || hidtrace::active()) {
      // Recording controls are silent. Suppress diagnostic console output while active.
    } else
#endif
    if (command == 'm') {
      console.printf("inputsReady=%u\n", unsigned(inputReady));
      console.printf("Boot: %s\nWi-Fi mode=%u status=%u AP=%s STA=%s\n", bootMessage.c_str(),
        unsigned(WiFi.getMode()), unsigned(WiFi.status()), WiFi.softAPIP().toString().c_str(), WiFi.localIP().toString().c_str());
      printDiagnostics(console, "manual");
    }
    else if (command == 'h') {
      console.setTxTimeoutMs(20);
      printMemoryDiagnostics(console);
      console.setTxTimeoutMs(0);
    }
    else if (command == 'd') {
      diagnosticsEnabled = !diagnosticsEnabled; lastDiagnostics = millis();
      console.println(diagnosticsEnabled ? "Diagnostics ON" : "Diagnostics OFF");
      if (diagnosticsEnabled) printDiagnostics(console, "enabled");
    } else if (command == '?') console.println("m=snapshot; h=startup/allocations/stacks; d=toggle 5s + save logging (default OFF); bytes; file -1=absent -2=open failed");
  }
  uint32_t now = millis();
  updateSetupPortal(wifiSetupFlow.tick(WiFi.status() == WL_CONNECTED, now));
  if (!maintenanceRecovery) backendsTick(now);
  uint32_t overflows = inputOverflows();
  if (overflows != observedOverflows) {
    observedOverflows = overflows; panic(); // Cancel pending work and release HID; no generated MIDI.
  }
  if (!restartAt && !factoryResetBlocked()) {
    if (configOutputsAllowed()) engine.tick(now);
    InputEvent event;
    for (unsigned budget = 0; budget < 16 && nextInput(event); ++budget) {
      hidtrace::record(hidtrace::InputConsumed, 255, event.input);
      if (configOutputsAllowed()) engine.trigger(event);
      lastActivity = now;
    }
  } else if (restartAt && static_cast<int32_t>(now - restartAt) >= 0) ESP.restart();
  if (!restartAt && !factoryResetBlocked() && configOutputsAllowed()) rotationSendTick();
  const uint32_t ledPhase = now % 1000;
  bool led = setupApReady ? ledPhase < 500 :
             WiFi.status() == WL_CONNECTED ? now - lastActivity > 50 :
             ledPhase < 100 || (ledPhase >= 200 && ledPhase < 300);
  setStatusLed(inputReady && led);
  if (diagnosticsEnabled && now - lastDiagnostics >= 5000
#if CHAINPAD_HID_TRACE
      && !hidtrace::active()
#endif
  ) {
    lastDiagnostics = now; printDiagnostics(console, "periodic");
  }
  delay(1);
#if CHAINPAD_HID_TRACE
  hidtrace::dumpTick(console);
#endif
}
