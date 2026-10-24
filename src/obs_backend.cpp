#include "obs_backend.h"
#if CHAINPAD_HAS_OBS
#include "obs_auth.h"
#include "obs_lifecycle.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>

namespace chimera {
namespace {
using namespace obs;
constexpr unsigned REQUESTS = 4, RESULTS = 8, STACK_BYTES = 8192;
struct Settings { ObsSettings value; uint32_t generation = 0; };
struct Request { uint32_t id; Epoch epoch; char scene[SCENE_LIMIT + 1]; };
StaticQueue_t requestControl, resultControl, settingControl;
uint8_t requestStorage[REQUESTS * sizeof(Request)], resultStorage[RESULTS * sizeof(ObsResult)], settingStorage[sizeof(Settings)];
QueueHandle_t requestQueue = nullptr, resultQueue = nullptr, settingQueue = nullptr;
StaticTask_t taskControl;
StackType_t taskStack[STACK_BYTES / sizeof(StackType_t)];
TaskHandle_t task = nullptr;
portMUX_TYPE gate = portMUX_INITIALIZER_UNLOCKED;
Lifecycle lifecycle;
struct Lock { Lock() { portENTER_CRITICAL(&gate); } ~Lock() { portEXIT_CRITICAL(&gate); } };
std::atomic<State> published{State::Disabled};
std::atomic<uint32_t> accepted{0}, rejected{0}, completed{0}, failed{0}, discarded{0}, lostResults{0}, attempts{0}, ignored{0}, stackLow{0};
std::atomic<int> lastError{0};
std::atomic<uint32_t> lastClose{0}, connectMs{0}, authMs{0}, responseMs{0};
uint32_t lastLoop = 0, maximumLoopUs = 0;
// Everything below belongs exclusively to the worker/callbacks.
Settings live;
obs::Protocol protocol;
Assembly assembly;
Epoch session;
bool fatal = false, identify = false, closed = false;
uint32_t started = 0, authenticatingAt = 0;
bool station() { return (WiFi.getMode() & WIFI_STA) && WiFi.status() == WL_CONNECTED; }
bool current() { Lock lock; return lifecycle.current(session); }
void result(uint32_t id, bool success, int code) {
  if (!id) return;
  ObsResult r{id, success, code};
  if (xQueueSend(resultQueue, &r, 0) != pdTRUE) ++lostResults;
  if (success) ++completed; else ++failed;
  if (code == -2) ++discarded;
}
void discard() {
  result(protocol.pending, false, -2); protocol.pending = 0;
  Request r;
  while (xQueueReceive(requestQueue, &r, 0) == pdTRUE) result(r.id, false, -2);
  assembly.reset(); identify = false;
}
void invalidate() {
  Lock lock; lifecycle.disconnected(); published = State::Disconnected;
}
class Socket : public WebSocketsClient {
  char header[513]{};
  size_t used = 0, total = 0;
protected:
  void messageReceived(WSclient_t* client, WSopcode_t opcode, uint8_t* payload, size_t size, bool fin) override {
    if (opcode == WSop_close) {
      uint16_t code = size >= 2 ? (uint16_t(payload[0]) << 8) | payload[1] : 1005;
      lastClose = code;
      if (terminalClose(code)) { fatal = true; lastError = code; }
      return;
    }
    WebSocketsClient::messageReceived(client, opcode, payload, size, fin);
  }
public:
  void halt() {
    clientDisconnect(&_client, static_cast<const char*>(nullptr)); _port = 0;
    used = total = 0;
  }
  void start() {
    halt(); begin(live.value.ip, live.value.port, "/", "obswebsocket.json");
    setReconnectInterval(0); enableHeartbeat(10000, 3000, 1);
  }
  bool alive() const { return _client.status != WSC_NOT_CONNECTED; }
  void pump() {
    if (_client.status != WSC_NOT_CONNECTED && !clientIsConnected(&_client)) { closed = true; return; }
    if (_client.status == WSC_HEADER) {
      if (!_client.tcp || !clientIsConnected(&_client)) return;
      for (unsigned budget = 0; budget < 128 && _client.tcp->available() > 0; ++budget) {
        int c = _client.tcp->read();
        if (c < 0) break;
        if (++total > 2048 || used == sizeof(header) - 1 || c == 0) { fatal = true; lastError = -8; return; }
        if (c == '\n') {
          header[used] = 0; String line(header); used = 0;
          handleHeader(&_client, &line);
          if (_client.status != WSC_HEADER) break;
        } else header[used++] = char(c);
      }
    } else WebSocketsClient::loop();
  }
};
void message(const char* bytes, size_t size) {
  if (!current()) return;
  switch (protocol.receive(bytes, size, live.value.password, authentication)) {
    case Received::Identify: identify = true; break;
    case Received::Ready: {
      Lock lock;
      if (lifecycle.publishReady(session)) { published = State::Ready; authMs = millis() - authenticatingAt; }
      break;
    }
    case Received::Result:
      responseMs = millis() - protocol.sentAt;
      result(protocol.pending, protocol.success, protocol.code); protocol.pending = 0; break;
    case Received::Ignored: ++ignored; break;
    case Received::Invalid: fatal = true; lastError = -7; break;
  }
}
void event(WStype_t type, uint8_t* data, size_t size) {
  switch (type) {
    case WStype_CONNECTED:
      protocol.connected(); published = State::Authenticating;
      authenticatingAt = millis(); connectMs = authenticatingAt - started; break;
    case WStype_DISCONNECTED: closed = true; break;
    case WStype_TEXT:
      if (assembly.active) { fatal = true; lastError = -8; }
      else message(reinterpret_cast<char*>(data), size);
      break;
    case WStype_FRAGMENT_TEXT_START: case WStype_FRAGMENT: case WStype_FRAGMENT_FIN:
      if (!assembly.append(data, size, type == WStype_FRAGMENT_TEXT_START, millis())) { fatal = true; lastError = -8; }
      else if (type == WStype_FRAGMENT_FIN) { message(assembly.bytes, assembly.length); assembly.reset(); }
      break;
    case WStype_PING: case WStype_PONG: break;
    default: fatal = true; lastError = -8; break;
  }
}
void worker(void*) {
  Socket socket; socket.onEvent(event);
  uint32_t nextAttempt = 0, backoff = 1000;
  bool active = false;
  for (;;) {
    stackLow = uxTaskGetStackHighWaterMark(nullptr);
    if (xQueueReceive(settingQueue, &live, 0) == pdTRUE) {
      invalidate(); socket.halt(); discard();
      active = false; fatal = false; closed = false; backoff = 1000; nextAttempt = millis(); lastError = 0;
    }
    bool available, configured; Epoch nowEpoch;
    {
      Lock lock; nowEpoch = lifecycle.epoch;
      configured = lifecycle.allowed && lifecycle.enabled;
      available = lifecycle.current(nowEpoch) && live.generation == nowEpoch.settings;
    }
    available = available && station();
    if (!available || (active && !(session == nowEpoch))) {
      if (active) { invalidate(); socket.halt(); }
      active = false; discard();
      // Terminal errors remain latched across Wi-Fi loss/Panic. Only explicit
      // settings application retries them. No attempts in AP-only mode.
      published = fatal ? State::Error : configured ? State::Disconnected : State::Disabled;
      nextAttempt = millis() + 1000;
    } else if (fatal) {
      if (active) { invalidate(); socket.halt(); }
      active = false; discard(); published = State::Error;
    } else {
      uint32_t now = millis();
      if (!active && int32_t(now - nextAttempt) >= 0) {
        { Lock lock; session = lifecycle.epoch; }
        closed = false; identify = false; assembly.reset();
        lastClose = 0; protocol.state = State::Connecting; published = State::Connecting; ++attempts;
        started = now; socket.start(); closed = false; active = true;
      }
      if (active) {
        socket.pump(); now = millis();
        if (identify && !fatal && current()) {
          identify = false;
          if (!socket.sendTXT(protocol.wire)) { closed = true; lastError = -3; }
        }
        if (protocol.expire(now)) {
          result(protocol.pending, false, -4); protocol.pending = 0;
          closed = true; lastError = -4;
        }
        if (assembly.expired(now) || (protocol.state != State::Ready && uint32_t(now - started) >= HANDSHAKE_TIMEOUT_MS)) {
          closed = true; lastError = -5;
        }
        if (fatal || closed || !socket.alive() || !current() || !station()) {
          if (!lastError.load()) lastError = -1;
          invalidate(); socket.halt(); discard(); active = false;
          published = fatal ? State::Error : State::Disconnected;
          nextAttempt = now + backoff; backoff = backoff < 16000 ? backoff * 2 : 30000;
        } else if (protocol.state == State::Ready && !protocol.pending) {
          backoff = 1000;
          Request r;
          if (xQueueReceive(requestQueue, &r, 0) == pdTRUE) {
            if (!(r.epoch == session) || !current()) result(r.id, false, -2);
            else if (!protocol.request(r.scene, r.id, now)) result(r.id, false, -6);
            else {
              bool claimed;
              { Lock lock; claimed = lifecycle.claim(r.epoch); }
              if (!claimed) { protocol.pending = 0; result(r.id, false, -2); }
              else if (!socket.sendTXT(protocol.wire)) {
                result(r.id, false, -3); protocol.pending = 0; closed = true;
              }
            }
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}
void obsBegin() {
  if (task) return;
  requestQueue = xQueueCreateStatic(REQUESTS, sizeof(Request), requestStorage, &requestControl);
  resultQueue = xQueueCreateStatic(RESULTS, sizeof(ObsResult), resultStorage, &resultControl);
  settingQueue = xQueueCreateStatic(1, sizeof(Settings), settingStorage, &settingControl);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { Lock lock; lifecycle.wifiLost(); }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (requestQueue && resultQueue && settingQueue)
    task = xTaskCreateStaticPinnedToCore(worker, "chainpad-obs", STACK_BYTES, nullptr, 1, taskStack, &taskControl, ARDUINO_RUNNING_CORE);
  if (!task) { lastError = -9; published = obs::State::Error; }
}
bool obsApplySettings(const ObsSettings& value) {
  IPAddress ip;
  if (!task || !obs::validText(value.ip, 15, !value.enabled) || (value.ip[0] && !ip.fromString(value.ip)) ||
      !value.port || !obs::validText(value.password, 128, true)) return false;
  Settings copy; copy.value = value;
  { Lock lock; lifecycle.settings(value.enabled); copy.generation = lifecycle.epoch.settings; }
  // Single loop producer. Worker always checks generation before acting.
  return xQueueOverwrite(settingQueue, &copy) == pdTRUE;
}
SendResult obsDispatch(const char* scene, uint32_t& id) {
  id = 0;
  if (!obs::validText(scene, obs::SCENE_LIMIT)) { ++rejected; return SendResult::Failed; }
  if (!task || !station()) { ++rejected; return SendResult::Unavailable; }
  Request r{}; memcpy(r.scene, scene, strlen(scene) + 1);
  Lock lock;
  if (!lifecycle.claim(lifecycle.epoch)) { ++rejected; return SendResult::Unavailable; }
  r.epoch = lifecycle.epoch; r.id = lifecycle.nextId();
  if (!r.id) { ++rejected; return SendResult::Failed; }
  if (xQueueSend(requestQueue, &r, 0) != pdTRUE) { ++rejected; return SendResult::Busy; }
  id = r.id; ++accepted; return SendResult::Accepted;
}
void obsPanic() { Lock lock; lifecycle.panic(); }
void obsTick(bool allowed) {
  { Lock lock; lifecycle.permit(allowed); }
  uint32_t now = micros();
  if (lastLoop && uint32_t(now - lastLoop) > maximumLoopUs) maximumLoopUs = now - lastLoop;
  lastLoop = now;
}
ObsStatus obsStatusSnapshot() {
  ObsStatus s; s.state = published.load();
  { Lock lock;
    if (!lifecycle.allowed || !lifecycle.enabled) s.state = obs::State::Disabled;
    else if (s.state == obs::State::Ready && !lifecycle.claim(lifecycle.epoch)) s.state = obs::State::Disconnected;
  }
  if (s.state == obs::State::Ready && !station()) s.state = obs::State::Disconnected;
  s.accepted = accepted; s.rejected = rejected; s.completed = completed; s.failed = failed; s.discarded = discarded;
  s.resultDrops = lostResults; s.attempts = attempts; s.ignored = ignored;
  s.queued = requestQueue ? uxQueueMessagesWaiting(requestQueue) : 0;
  s.stackLowBytes = stackLow; s.loopMaxUs = maximumLoopUs;
  s.connectMs = connectMs; s.authMs = authMs; s.responseMs = responseMs;
  s.lastError = lastError; s.lastClose = lastClose; return s;
}
bool obsNextResult(ObsResult& r) { return resultQueue && xQueueReceive(resultQueue, &r, 0) == pdTRUE; }
}
#endif
