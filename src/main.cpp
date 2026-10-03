#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "model.h"
#if CHAINPAD_HAS_USB
#include <USBCDC.h>
#endif
#include "engine.h"
#include "inputs.h"
#include "backends.h"
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
bool inputReady = false;
uint32_t restartAt = 0, lastActivity = 0;
char bootSsid[33]{}, bootPassword[65]{};
void jsonResponse(int status, JsonDocument& doc) {
  String body; serializeJson(doc, body);
  web.sendHeader("Cache-Control", "no-store");
  web.send(status, "application/json; charset=utf-8", body);
}
void result(int status, const String& message) {
  JsonDocument doc; doc["ok"] = status < 400; doc["message"] = message; jsonResponse(status, doc);
}
void panic() { backendsPanic(); discardInputs(); }
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
    String error;
    if (!saveWifiConfig(web.arg("plain"), error)) { result(400, error); return; }
    panic(); restartAt = millis() + 750;
    result(200, "Saved. Restarting.");
  });
  web.on("/api/capabilities", HTTP_GET, [] {
    JsonDocument doc; encodeCapabilities(doc); jsonResponse(200, doc);
  });
  web.on("/api/config", HTTP_GET, [] {
    JsonDocument doc; encodeConfig(config, doc); jsonResponse(200, doc);
  });
  web.on("/api/config", HTTP_PUT, [] {
    String error;
    if (!saveConfig(web.arg("plain"), error)) { result(400, error); return; }
    panic();
    result(200, "Saved and applied. Wi-Fi credential changes require Restart.");
  });
  web.on("/api/status", HTTP_GET, [] {
    JsonDocument doc;
    auto s = backendStatus();
    doc["name"] = "ChainPad"; doc["version"] = "0.2.1-chimera";
    doc["hardware"] = HARDWARE_NAME;
    doc["usbMidiSupported"] = HAS_USB_MIDI; doc["usbKeyboardSupported"] = HAS_USB_KEYBOARD;
    doc["keyCount"] = 12; doc["encoder"] = true; doc["encoderPush"] = true; doc["led"] = true;
    doc["wifi"] = s.wifi; doc["ip"] = WiFi.localIP().toString(); doc["apIp"] = WiFi.softAPIP().toString();
    doc["captivePortal"] = portalReady;
    doc["usbMidi"] = s.usbMidi; doc["usbKeyboard"] = s.usbKeyboard;
    doc["bleMidi"] = s.bleMidi; doc["bleKeyboard"] = s.bleKeyboard;
    doc["events"] = engine.stats.events; doc["accepted"] = engine.stats.accepted;
    doc["skipped"] = engine.stats.skipped;
    doc["lastInput"] = inputName(engine.stats.lastInput);
    doc["lastAction"] = engine.stats.lastAction + 1;
    const char* results[] = {"accepted", "unavailable", "busy", "failed"};
    doc["lastResult"] = results[unsigned(engine.stats.lastResult)];
    doc["transportRetries"] = s.retries; doc["transportOverflows"] = s.overflows;
    doc["inputOverflows"] = inputOverflows(); doc["pressedMask"] = pressedInputs();
    doc["inputsReady"] = inputReady; doc["bootMessage"] = bootMessage;
    doc["restartRequired"] = strcmp(config.ssid, bootSsid) != 0 || strcmp(config.password, bootPassword) != 0;
    doc["freeHeap"] = ESP.getFreeHeap(); doc["uptimeMs"] = millis();
    jsonResponse(200, doc);
  });
  web.on("/api/trigger", HTTP_POST, [] {
    JsonDocument doc;
    if (deserializeJson(doc, web.arg("plain")) || !doc["input"].is<unsigned>() || doc["input"].as<unsigned>() >= INPUT_COUNT) {
      result(400, "input must be 0..27"); return;
    }
    engine.trigger({doc["input"].as<uint8_t>(), millis()}); lastActivity = millis();
    result(200, "Saved chain dispatched; see status for transport results.");
  });
  web.on("/api/panic", HTTP_POST, [] { panic(); result(200, "All outputs reset; queued input events discarded."); });
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
  console.println("ChainPad / Project Chimera Phase 1"); console.println(bootMessage);
  console.println("Setup: ChainPad-Setup / http://192.168.4.1");
}
void loop() {
  static uint32_t observedOverflows = 0;
  web.handleClient();
  for (unsigned i = 0; i < 64 && console.available() > 0; ++i) console.read();
  uint32_t now = millis();
  backendsTick(now);
  uint32_t overflows = inputOverflows();
  if (overflows != observedOverflows) {
    observedOverflows = overflows; panic(); // A dropped release cannot strand notes/keys.
  }
  if (!restartAt) {
    InputEvent event;
    for (unsigned budget = 0; budget < 16 && nextInput(event); ++budget) {
      engine.trigger(event); lastActivity = now;
    }
  } else if (static_cast<int32_t>(now - restartAt) >= 0) ESP.restart();
  bool led = WiFi.status() == WL_CONNECTED ? now - lastActivity > 50 : now % 1000 < 500;
  setStatusLed(inputReady && led);
  delay(1);
}
