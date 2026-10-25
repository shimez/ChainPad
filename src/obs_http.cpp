#include "obs_http.h"
#if CHAINPAD_HAS_OBS && !defined(CHAINPAD_OBS_TEST)
#include <memory>
#include <new>
#include <cstring>
#include <cstdio>
namespace chimera {
namespace {
class ObsHandler : public RequestHandler {
  obscontrol::Controller& control;
  bool (*maintenance)();
  bool (*outputsAllowed)();
  std::unique_ptr<char[]> body;
  size_t used = 0;
  bool complete = false;
  bool repliedEarly = false;
  static bool route(const String& uri) {
    return !strcmp(uri.c_str(), "/api/obs") || !strcmp(uri.c_str(), "/api/obs/reconnect");
  }
  void clearBody() {
    if (body) { volatile char* p = body.get(); for (size_t i = 0; i < used; ++i) p[i] = 0; }
    body.reset(); used = 0; complete = false;
  }
  void send(WebServer& web, obscontrol::Reply reply, bool get = false) {
    web.sendHeader("Cache-Control", "no-store");
    JsonDocument doc;
    control.settingsJson(doc.to<JsonObject>());
    if (!get) {
      doc["ok"] = reply.http < 400; doc["saved"] = reply.saved;
      doc["error"] = nullptr;
      if (reply.error) {
        auto error = doc["error"].to<JsonObject>();
        error["code"] = reply.error; error["message"] = "OBS operation rejected or failed; inspect state before retry";
      }
    }
    constexpr size_t capacity = 768;
    std::unique_ptr<char[]> output(new (std::nothrow) char[capacity]);
    const size_t size = measureJson(doc);
    if (!output || doc.overflowed() || size >= capacity || serializeJson(doc, output.get(), capacity) != size) {
      // Preserve whether Flash committed, even if response allocation fails.
      char fallback[192];
      snprintf(fallback, sizeof(fallback), "{\"ok\":false,\"saved\":%s,\"applyState\":\"%s\",\"error\":{\"code\":\"responseFailed\",\"message\":\"Response encoding failed\"}}",
               reply.saved ? "true" : "false", control.applyName());
      web.send(reply.http >= 400 ? reply.http : 500, "application/json; charset=utf-8", fallback); return;
    }
    web.send(reply.http, "application/json; charset=utf-8", output.get());
  }
public:
  ObsHandler(obscontrol::Controller& c, bool (*m)(), bool (*a)()) : control(c), maintenance(m), outputsAllowed(a) {}
  bool canHandle(WebServer&, HTTPMethod, const String& uri) override {
    if (!route(uri)) return false;
    clearBody(); repliedEarly = false; return true;
  }
  bool canRaw(WebServer&, const String&) override { return true; }
  void raw(WebServer& web, const String& uri, HTTPRaw& raw) override {
    if (raw.status == RAW_START) {
      clearBody();
      const int size = web.clientContentLength();
      const bool put = !strcmp(uri.c_str(), "/api/obs") && web.method() == HTTP_PUT;
      const auto type = web.header("Content-Type");
      const bool json = !strcmp(type.c_str(), "application/json") || !strcmp(type.c_str(), "application/json; charset=utf-8");
      if (size < 0 || size > int(obssettings::MaxJsonBytes) || (put && !json) || (!put && size != 0) || web.hasHeader("Transfer-Encoding")) {
        send(web, {400, false, "invalidRequest"}); repliedEarly = true; web.client().stop(); return;
      }
      body.reset(new (std::nothrow) char[obssettings::MaxJsonBytes + 1]);
      if (!body) { send(web, {500, false, "noMemory"}); repliedEarly = true; web.client().stop(); }
    } else if (raw.status == RAW_WRITE) {
      if (!body || raw.currentSize > obssettings::MaxJsonBytes - used) {
        clearBody(); web.client().stop(); return;
      }
      memcpy(body.get() + used, raw.buf, raw.currentSize); used += raw.currentSize;
    } else if (raw.status == RAW_END) {
      if (body) { body[used] = 0; complete = true; }
    } else if (raw.status == RAW_ABORTED) clearBody();
  }
  bool handle(WebServer& web, HTTPMethod method, const String& uri) override {
    if (!route(uri)) return false;
    if (repliedEarly) { clearBody(); return true; }
    control.tick(outputsAllowed());
    const bool settings = !strcmp(uri.c_str(), "/api/obs");
    if (settings && method == HTTP_GET) send(web, {200, false, nullptr}, true);
    else if ((settings && method == HTTP_PUT) || (!settings && method == HTTP_POST)) {
      if (maintenance()) send(web, {409, false, "maintenance"});
      else if (!complete) send(web, {400, false, "invalidRequest"});
      else send(web, settings ? control.put(body.get(), used, false) : control.reconnect(false));
    } else send(web, {405, false, "methodNotAllowed"});
    clearBody(); return true;
  }
};
}
void registerObsHttp(WebServer& web, obscontrol::Controller& control, bool (*maintenance)(), bool (*outputsAllowed)()) {
  const char* headers[] = {"Content-Type", "Transfer-Encoding"};
  web.collectHeaders(headers, 2);
  web.addHandler(new ObsHandler(control, maintenance, outputsAllowed));
}
}
#endif
