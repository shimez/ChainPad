#include "obs_test_console.h"
#if defined(CHAINPAD_OBS_TEST)
#include "obs_backend.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform_util.h>

namespace chimera {
namespace {
// Test-only JSON lines prefixed with '!'. Never echo lines or server text.
char line[1025]{};
size_t used = 0;
bool reading = false, overflow = false;
void command(Print& out) {
  JsonDocument d;
  if (overflow || deserializeJson(d, line, DeserializationOption::NestingLimit(2)) || !d.is<JsonObject>()) {
    out.println("OBS TEST invalid command"); return;
  }
  const char* op = d["op"] | "";
  if (!strcmp(op, "settings")) {
    ObsSettings s;
    const char* ip = d["ip"], *password = d["password"];
    if (!d["enabled"].is<bool>() || !d["port"].is<unsigned>() || !d["port"].as<unsigned>() || d["port"].as<unsigned>() > 65535 ||
        !obs::validText(ip, 15, true) || !obs::validText(password, 128, true) ||
        d["ip"].as<JsonString>().size() != strlen(ip) || d["password"].as<JsonString>().size() != strlen(password)) {
      out.println("OBS TEST invalid settings"); return;
    }
    strcpy(s.ip, ip); strcpy(s.password, password); s.port = d["port"]; s.enabled = d["enabled"];
    out.println(obsApplySettings(s) ? "OBS TEST settings accepted (RAM only)" : "OBS TEST settings rejected");
    mbedtls_platform_zeroize(&s, sizeof(s));
  } else if (!strcmp(op, "scene")) {
    const char* scene = d["name"];
    if (!obs::validText(scene, obs::SCENE_LIMIT) || d["name"].as<JsonString>().size() != strlen(scene)) { out.println("OBS TEST invalid scene"); return; }
    unsigned count = d["count"] | 1u;
    if (!count || count > 16) { out.println("OBS TEST invalid count"); return; }
    for (unsigned i = 0; i < count; ++i) {
      uint32_t id; auto r = obsDispatch(scene, id);
      out.printf("OBS TEST admission=%u id=%lu (0=accepted,1=unavailable,2=busy,3=failed; NOT OBS success)\n", unsigned(r), id);
    }
  } else if (!strcmp(op, "panic")) {
    obsPanic(); out.println("OBS TEST pending work invalidated; in-flight outcome may be unknown");
  } else if (!strcmp(op, "status")) {
    auto s = obsStatusSnapshot();
    out.printf("OBS TEST state=%u error=%d close=%lu attempts=%lu queued=%lu accepted=%lu rejected=%lu success=%lu failed=%lu discarded=%lu resultDrops=%lu\n",
      unsigned(s.state), s.lastError, s.lastClose, s.attempts, s.queued, s.accepted, s.rejected, s.completed, s.failed, s.discarded, s.resultDrops);
    constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    out.printf("OBS TEST internal=%u min=%u largest=%u psramFree=%u psramUsed=%u stackLowBytes=%lu loopMaxUs=%lu connectMs=%lu authMs=%lu responseMs=%lu\n",
      unsigned(heap_caps_get_free_size(caps)), unsigned(heap_caps_get_minimum_free_size(caps)), unsigned(heap_caps_get_largest_free_block(caps)),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      s.stackLowBytes, s.loopMaxUs, s.connectMs, s.authMs, s.responseMs);
    ObsResult r;
    for (unsigned n = 0; n < 8 && obsNextResult(r); ++n) out.printf("OBS TEST result id=%lu success=%u code=%d\n", r.id, unsigned(r.success), r.code);
  } else out.println("OBS TEST unknown operation");
}
}
bool obsTestByte(int value, Print& out) {
  if (!reading) { if (value != '!') return false; reading = true; return true; }
  if (value == '\r') return true;
  if (value == '\n' || value == 27) {
    if (value == '\n') command(out);
    mbedtls_platform_zeroize(line, sizeof(line)); used = 0; reading = overflow = false;
  } else if (value < 32 || value == 127) overflow = true;
  else if (used < sizeof(line) - 1 && !overflow) line[used++] = char(value);
  else overflow = true;
  return true;
}
}
#endif
