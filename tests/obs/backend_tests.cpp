#include "obs_backend.h"
#include "obs_lifecycle.h"
#include "obs_actions.h"
#include "engine.h"
#include "rotation_runtime.h"
#include <WebSocketsClient.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <ArduinoJson.h>
#include <iostream>
using namespace chimera;
// Production Engine/dispatch/OBS adapter/Worker; unrelated output transports are sinks.
static unsigned hidSent=0, oscSent=0;
namespace chimera {
Config config; RotationRuntime rotationRuntime;
bool RotationRuntime::step(bool) { return false; }
void transportsBegin() {} void transportsTick() {} void rotationSenderBegin() {} void rotationDiscard() {}
void midiTick() {} void keyboardTick() {} void midiPanic() {} void keyboardPanic() {}
bool midiReady(Transport) {return true;} bool keyboardReady(Transport) {return true;}
bool midiQueued(Transport) {return false;} bool keyboardQueued(Transport) {return false;}
SendResult midiDispatch(const Action&) {return SendResult::Accepted;}
SendResult keyboardDispatch(const Action&) {++hidSent;return SendResult::Accepted;}
SendResult oscDispatch(const Action&) {++oscSent;return SendResult::Accepted;}
}
static ObsSettings settings() {ObsSettings s;strcpy(s.ip,"192.0.2.1");strcpy(s.password,"test-password");s.enabled=true;return s;}
static void ready() {
  for(int i=0;i<40&&obsStatusSnapshot().state!=obs::State::Ready;++i)rtos::step(1000);
  assert(obsStatusSnapshot().state==obs::State::Ready);
}
static size_t requests(){size_t n=0;for(auto& s:wire::sent)if(s.find("\"op\":6")!=std::string::npos)++n;return n;}
static uint32_t submit(const char* name="scene"){uint32_t id;assert(obsDispatch(name,id)==SendResult::Accepted&&id);return id;}
static void response(uint32_t id,bool success=true,int code=100){
  wire::text("{\"op\":7,\"d\":{\"requestId\":\"chainpad-"+std::to_string(id)+"\",\"requestType\":\"SetCurrentProgramScene\",\"requestStatus\":{\"result\":"+(success?"true":"false")+",\"code\":"+std::to_string(code)+"}}}");rtos::step();
}
int main(){
  obs::Lifecycle l;l.permit(true);l.settings(true);auto e=l.epoch;assert(l.publishReady(e)&&l.claim(e));
  l.panic();assert(!l.claim(e)&&!l.publishReady(e));e=l.epoch;assert(l.publishReady(e));
  l.wifiLost();assert(!l.claim(e));e=l.epoch;l.publishReady(e);l.settings(true);assert(!l.claim(e));
  l.epoch.execution=UINT32_MAX;l.panic();assert(!l.publishReady(l.epoch));
  obs::Lifecycle ids;ids.sequence=UINT32_MAX;assert(!ids.nextId());
  obsBegin();obsTick(true);uint32_t id=77;
  assert(obsDispatch("scene",id)==SendResult::Unavailable&&id==0);
  assert(obsDispatch("",id)==SendResult::Failed);
  auto cfg=settings();assert(obsApplySettings(cfg));strcpy(cfg.password,"mutated-caller-copy");
  ready();assert(wire::sent.front().find("zBIloly70ANx+T9nUkQVuTCqTUuHtfrqEdn8XDQ3m/s=")!=std::string::npos);
  for(int i=0;i<4;++i)submit();assert(obsDispatch("full",id)==SendResult::Busy&&id==0);
  auto before=requests();obsPanic();assert(obsDispatch("after-panic",id)==SendResult::Unavailable);
  rtos::step();ready();assert(requests()==before&&obsStatusSnapshot().discarded>=4);
  id=submit();rtos::step();assert(requests()==before+1);response(id);assert(obsStatusSnapshot().completed==1);
  id=submit("missing");rtos::step();response(id,false,600);
  ObsResult result;bool missing=false;while(obsNextResult(result))if(result.id==id){missing=true;assert(!result.success&&result.code==600);}assert(missing);
  // Injection after the worker's session validation but before dequeue/encoding.
  before=requests();submit();rtos::beforeReceive=[] {obsPanic();};rtos::step();rtos::step();ready();assert(requests()==before);
  // Panic at the actual send boundary: it is already claimed/in-flight. No
  // promise of external cancellation; all following queued requests are retired.
  submit();submit();wire::beforeSend=[] {obsPanic();};rtos::step();rtos::step();ready();assert(requests()==before+1);
  before=requests();submit();cfg=settings();strcpy(cfg.ip,"192.0.2.2");assert(obsApplySettings(cfg));
  assert(obsStatusSnapshot().state!=obs::State::Ready); // M2 must not expose the old generation's Ready.
  assert(obsDispatch("new-before-ready",id)==SendResult::Unavailable);rtos::step();ready();assert(requests()==before&&wire::hosts.back()=="192.0.2.2");
  submit();WiFi.lose();WiFi.state=WL_CONNECTED; // Lost+recovered BETWEEN worker iterations.
  assert(obsDispatch("stale-ready",id)==SendResult::Unavailable);rtos::step();ready();assert(requests()==before);
  submit();WiFi.lose();rtos::step();WiFi.mode=WIFI_AP;
  auto attempts=obsStatusSnapshot().attempts;for(int i=0;i<5;++i)rtos::step(30000);
  assert(obsStatusSnapshot().attempts==attempts&&requests()==before);
  WiFi.mode=WIFI_STA;WiFi.state=WL_CONNECTED;ready();assert(requests()==before);
  submit();wire::online=false;rtos::step();assert(obsDispatch("offline",id)==SendResult::Unavailable);
  wire::online=true;ready();assert(requests()==before);
  id=submit();rtos::step();submit();before=requests();rtos::step(3001);ready();assert(requests()==before);
  while(obsNextResult(result)){} // Clear diagnostics before timeout check below.
  id=submit();rtos::step();rtos::step(3001);bool timeout=false;while(obsNextResult(result))if(result.id==id){assert(result.code==-4);timeout=true;}assert(timeout);ready();
  // Authentication failure/Kick latch across Wi-Fi loss and Panic; explicit retry.
  for(unsigned code:{4009u,4011u}){
    wire::closeCode=uint16_t(code);rtos::step();assert(obsStatusSnapshot().state==obs::State::Error);
    attempts=obsStatusSnapshot().attempts;obsPanic();WiFi.lose();rtos::step();WiFi.state=WL_CONNECTED;
    for(int i=0;i<3;++i)rtos::step(30000);assert(obsStatusSnapshot().attempts==attempts);
    assert(obsApplySettings(settings()));rtos::step();ready();
  }
  wire::text(std::string(2049,'x'));rtos::step();assert(obsStatusSnapshot().state==obs::State::Error);
  wire::auth=false;cfg=settings();cfg.password[0]=0;assert(obsApplySettings(cfg));rtos::step();ready();
  assert(wire::sent.back().find("authentication")==std::string::npos);
  // Permission loss blocks immediately, independent of worker scheduling.
  before=requests();submit();obsTick(false);assert(obsDispatch("blocked",id)==SendResult::Unavailable);
  rtos::step();obsTick(true);ready();assert(requests()==before);
  // Bounded result FIFO overflow does not stall request processing.
  while(obsNextResult(result)){}
  for(int i=0;i<12;++i){id=submit();rtos::step();response(id);}
  assert(obsStatusSnapshot().resultDrops>=4);
  // Real production header pump: per-line, total and NUL limits.
  for(const auto& headers:{std::string(513,'x'),std::string(1,'\0'),std::string(2050,'\n')}){
    wire::headers=headers;assert(obsApplySettings(settings()));
    for(int i=0;i<25&&obsStatusSnapshot().state!=obs::State::Error;++i)rtos::step(10);
    assert(obsStatusSnapshot().state==obs::State::Error&&obsStatusSnapshot().lastError==-8);
    wire::headers.clear();assert(obsApplySettings(settings()));rtos::step();ready();
  }
  // A stalled HTTP header after a previously Ready session must still expire.
  wire::headers="unfinished";assert(obsApplySettings(settings()));rtos::step();rtos::step(5001);
  assert(obsStatusSnapshot().lastError==-5);wire::headers.clear();assert(obsApplySettings(settings()));rtos::step();ready();
  wire::auth=true;cfg=settings();strcpy(cfg.password,"wrong-password");assert(obsApplySettings(cfg));
  rtos::step();rtos::step();assert(obsStatusSnapshot().state==obs::State::Error&&obsStatusSnapshot().lastClose==4009);
  assert(obsApplySettings(settings()));rtos::step();ready();
  // Retry timing remains bounded and capped, rather than a busy reconnect loop.
  wire::online=false;rtos::step();
  for(unsigned wait:{1000u,2000u,4000u,8000u,16000u,30000u,30000u}){
    attempts=obsStatusSnapshot().attempts;rtos::step(wait-1);assert(obsStatusSnapshot().attempts==attempts);
    rtos::step(1);assert(obsStatusSnapshot().attempts==attempts+1);
  }
  wire::online=true;ready();
  // All seven operations traverse the real Engine -> dispatch -> Worker path.
  obsActionsTick(); const auto drops=obsStatusSnapshot().resultDrops;
  for(unsigned i=0;i<21;++i) {
    const auto op=obs::Operation(i%7);
    auto& chain=config.chains[0]; chain.count=3;
    chain.actions[0].protocol=Protocol::Keyboard;
    auto& action=chain.actions[1]; action.protocol=Protocol::Obs; action.obsOperation=op;
    strcpy(action.obsTarget,"日本語 \"input\""); action.boolValue=op==obs::Operation::SetInputMute;
    chain.actions[2].protocol=Protocol::Osc;
    const auto sent=requests(); const auto completed=obsActionStatus().completed, failed=obsActionStatus().failed;
    assert(engine.trigger({0,0}) && !engine.activeCount());
    id=obsActionStatus().requestId; assert(id && !strcmp(obsActionStatus().admission,"accepted"));
    assert(hidSent==i+1 && oscSent==i+1 && requests()==sent); // no Worker step yet
    rtos::step(); assert(requests()==sent+1);
    JsonDocument packet; assert(!deserializeJson(packet,wire::sent.back()));
    assert(packet["d"]["requestType"]==obs::requestType(op));
    auto data=packet["d"]["requestData"];
    if(obs::named(op)) assert(data[op==obs::Operation::SetCurrentProgramScene?"sceneName":"inputName"]==action.obsTarget);
    else assert(data.isNull());
    if(op==obs::Operation::SetInputMute) assert(data["inputMuted"].is<bool>() && data["inputMuted"]==true);
    // StopOtherChains must leave accepted/in-flight requests alone.
    config.chains[2].count=1; config.chains[2].actions[0].protocol=Protocol::StopOtherChains;
    assert(engine.trigger({2,0}));
    const bool ok=i%2==0;
    wire::text("{\"op\":7,\"d\":{\"requestId\":\"chainpad-"+std::to_string(id)+"\",\"requestType\":\""+obs::requestType(op)+"\",\"requestStatus\":{\"result\":"+(ok?"true":"false")+",\"code\":"+(ok?"100":"600")+"}}}");rtos::step();
    backendsTick(0);
    assert(obsActionStatus().last.id==id && obsActionStatus().last.operation==op && obsActionStatus().last.success==ok);
    assert(obsActionStatus().completed==completed+ok && obsActionStatus().failed==failed+!ok);
    assert(!obsNextResult(result));
  }
  assert(obsStatusSnapshot().resultDrops==drops);
  // Lost Toggle reply: timeout is diagnostic, never a second toggle after reconnect.
  assert(engine.trigger({0,0})); id=obsActionStatus().requestId;rtos::step();before=requests();
  rtos::step(3001);ready();backendsTick(0);
  assert(requests()==before && obsActionStatus().last.id==id && obsActionStatus().last.code==-4);
  assert(obsActionStatus().last.operation==obs::Operation::ToggleInputMute);
  // Disconnect with a typed in-flight request also never replays it.
  assert(engine.trigger({0,0})); id=obsActionStatus().requestId;rtos::step();before=requests();
  wire::online=false;rtos::step();wire::online=true;ready();backendsTick(0);
  assert(requests()==before && obsActionStatus().last.id==id && !obsActionStatus().last.success);
  // Full/unavailable admission still continues to the OSC sink; Panic drains typed requests.
  for(int i=0;i<4;++i)submit();
  auto oscBefore=oscSent; assert(engine.trigger({0,0}));
  assert(oscSent==oscBefore+1 && !strcmp(obsActionStatus().admission,"busy"));
  before=requests(); backendsPanic(); assert(engine.trigger({0,0}));
  assert(oscSent==oscBefore+2 && !strcmp(obsActionStatus().admission,"disconnected"));
  rtos::step();ready();backendsTick(0);assert(requests()==before);
  cfg=settings();cfg.enabled=false;assert(obsApplySettings(cfg));rtos::step();assert(obsStatusSnapshot().state==obs::State::Disabled);
  oscBefore=oscSent;assert(engine.trigger({0,0}));
  assert(oscSent==oscBefore+1 && !strcmp(obsActionStatus().admission,"disabled"));
  rtos::shutdown();
  std::cout<<"PASS production OBS worker/API: settings snapshots, zero-wait queue/full, results, Panic races/in-flight boundary, config/Wi-Fi/session epochs, AP-only, reconnect, timeout/no replay, terminal Close latch/retry, oversize, no-auth, permission, result overflow, worker-only Socket ownership\n";
}
