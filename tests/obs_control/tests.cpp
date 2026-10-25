#define CHAINPAD_OBS_CONTROL_TEST
#include "../obs_settings/tests.cpp"
#include "obs_control.h"
#include "obs_http.h"
namespace fake {
bool accepted=true, allowed=false, maintenance=false, global=true;
unsigned applies=0, ticks=0;
SendResult sceneAdmission=SendResult::Unavailable;unsigned sceneRequests=0;
ObsSettings applied;
ObsStatus status;
void reset(){accepted=true;allowed=false;maintenance=false;global=true;applies=ticks=0;applied={};status={};}
}
namespace chimera {
bool obsApplySettings(const ObsSettings& s){
  ++fake::applies;
  if(!fake::accepted)return false;
  fake::applied=s;
  // Model existing public snapshot's epoch filtering, tested against real Worker separately.
  fake::status.state=s.enabled?obs::State::Disconnected:obs::State::Disabled;
  return true;
}
void obsTick(bool permitted){fake::allowed=permitted;++fake::ticks;}
ObsStatus obsStatusSnapshot(){auto s=fake::status;if(!fake::allowed||!fake::applied.enabled)s.state=obs::State::Disabled;return s;}
SendResult obsDispatch(const obs::Command& command,uint32_t& id){
  assert(command.operation==obs::Operation::GetSceneList);
  id=fake::sceneAdmission==SendResult::Accepted?++fake::sceneRequests:0;return fake::sceneAdmission;
}
bool obsNextResult(ObsResult&){return false;}
bool obsTakeSceneList(obs::SceneList&){return false;}
}
using chimera::obscontrol::Controller;
bool maintenance(){return fake::maintenance;}
bool outputs(){return fake::global&&!fake::maintenance;}
JsonDocument response(WebServer& web,int status){
  assert(web.code==status&&web.responseHeaders["Cache-Control"]=="no-store");
  assert(web.response.find("fixture-private")==std::string::npos);
  JsonDocument doc;assert(!deserializeJson(doc,web.response));return doc;
}
void seed(){Store s;assert(save(s,update)==Error::None);}
int main(){
  for(int state=0;state<5;++state){
    io::reset();fake::reset();
    if(state==0)seed();
    if(state==2)io::files[io::Active]="{";
    if(state==3)io::files[io::Active]="{\"storageVersion\":2}";
    if(state==4)io::probeError=true;
    const int writes=io::writes;Controller control;control.begin();control.tick(true);
    WebServer web;registerObsHttp(web,control,maintenance,outputs);web.request(HTTP_GET,"/api/obs");
    auto doc=response(web,200);
    const char* states[]={"ready","missing","corrupt","unsupportedVersion","ioError"};
    assert(doc["storageState"]==states[state]&&doc["settingsAvailable"]==(state<2));
    assert(io::writes==writes&&fake::applies==(state<2?1u:0u));
    if(state>=2){assert(doc["settings"].isNull()&&!fake::allowed);control.tick(true);assert(!fake::allowed);}
    else {assert(doc["settings"]["password"].isUnbound());assert(fake::applied.enabled==(state==0));}
    fake::status.generation=42+state;
    JsonDocument status;control.statusJson(status.to<JsonObject>());
    assert(status["generation"]==42+state);
    assert(status["storageState"]==doc["storageState"]&&status["applyState"]==doc["applyState"]);
    io::intact();
  }
  std::cout<<"PASS boot storage matrix, GET privacy and status consistency\n";
  io::reset();fake::reset();seed();fake::accepted=false;
  Controller bootFailed;bootFailed.begin();bootFailed.tick(true);
  assert(!fake::allowed&&!strcmp(bootFailed.applyName(),"failed"));
  io::reset();fake::reset();Controller control;control.begin();control.tick(true);
  control.observeLoop(UINT32_MAX-99);control.observeLoop(200);
  JsonDocument timing;control.statusJson(timing.to<JsonObject>());assert(timing["loopIntervalMaxUs"]==300);
  WebServer web;registerObsHttp(web,control,maintenance,outputs);
  web.request(HTTP_GET,"/api/obs/scenes");assert(response(web,200)["complete"]==false);
  web.request(HTTP_POST,"/api/obs/scenes/refresh");assert(response(web,503)["state"]=="error");
  web.request(HTTP_GET,"/api/obs/scenes/refresh");response(web,405);
  web.request(HTTP_POST,"/api/obs/scenes/refresh","{}");response(web,400);
  fake::maintenance=true;web.request(HTTP_POST,"/api/obs/scenes/refresh");response(web,409);fake::maintenance=false;
  fake::sceneAdmission=SendResult::Busy;web.request(HTTP_POST,"/api/obs/scenes/refresh");response(web,429);
  fake::sceneAdmission=SendResult::Accepted;web.request(HTTP_POST,"/api/obs/scenes/refresh");assert(response(web,202)["state"]=="loading");
  web.request(HTTP_POST,"/api/obs/scenes/refresh");response(web,202);assert(fake::sceneRequests==1);
  web.request(HTTP_GET,"/api/obs/scenes");assert(response(web,200)["complete"]==false);
  web.request(HTTP_POST,"/api/obs/reconnect");assert(response(web,409)["error"]["code"]=="disabled");
  web.request(HTTP_PUT,"/api/obs",update);assert(response(web,200)["saved"]==true&&fake::allowed);
  auto old=io::files.at(io::Active);unsigned applies=fake::applies;
  web.request(HTTP_PUT,"/api/obs","{}");assert(response(web,400)["saved"]==false);
  assert(io::files.at(io::Active)==old&&fake::applies==applies&&fake::allowed);
  for(auto body:{std::string(1025,'x'),update+std::string(1,'\0'),std::string("{\"replaceInvalid\":null}")}){
    web.request(HTTP_PUT,"/api/obs",body);response(web,400);assert(io::files.at(io::Active)==old&&fake::applies==applies);
  }
  auto floatPort=update;floatPort.replace(floatPort.find("4455"),4,"4455.0");
  web.request(HTTP_PUT,"/api/obs",floatPort);response(web,400);
  assert(io::files.at(io::Active)==old&&fake::applies==applies);
  web.request(HTTP_PUT,"/api/obs",update,"text/plain");response(web,400);
  web.request(HTTP_PUT,"/api/obs",update,"application/json",true);assert(!web.code&&fake::applies==applies);
  web.request(HTTP_DELETE,"/api/obs");response(web,405);
  fake::maintenance=true;web.request(HTTP_PUT,"/api/obs",update);response(web,409);
  web.request(HTTP_POST,"/api/obs/reconnect");response(web,409);assert(fake::applies==applies&&!fake::allowed);
  fake::maintenance=false;
  auto enabledOmit=update;enabledOmit.erase(enabledOmit.find(",\"password\""));enabledOmit+='}';
  web.request(HTTP_PUT,"/api/obs",enabledOmit);response(web,200);assert(!strcmp(fake::applied.password,"fixture-private"));
  auto changed=enabledOmit;changed.insert(changed.size()-1,",\"password\":\"replacement-private\"");
  web.request(HTTP_PUT,"/api/obs",changed);response(web,200);assert(!strcmp(fake::applied.password,"replacement-private"));
  assert(web.response.find("replacement-private")==std::string::npos);
  auto clear=enabledOmit;clear.insert(clear.size()-1,",\"password\":\"\"");
  web.request(HTTP_PUT,"/api/obs",clear);response(web,200);assert(!fake::applied.password[0]);
  // Persisted enabled snapshot survives reboot.
  Controller reboot;reboot.begin();reboot.tick(true);assert(fake::applied.enabled);
  int writes=io::writes;
  for(auto state:{obs::State::Ready,obs::State::Error}){
    fake::status.state=state;
    web.request(HTTP_POST,"/api/obs/reconnect");response(web,202);assert(io::writes==writes);
    JsonDocument status;control.statusJson(status.to<JsonObject>());assert(status["connectionState"]!="Ready");
  }
  fake::global=false;control.tick(false);assert(!fake::allowed);control.tick(true);assert(fake::allowed);fake::global=true;
  fake::accepted=false;applies=fake::applies;
  web.request(HTTP_PUT,"/api/obs",update);auto failed=response(web,503);
  assert(failed["saved"]==true&&failed["applyState"]=="failed"&&fake::applies==applies+1&&!fake::allowed);
  for(int i=0;i<3;++i){control.tick(true);assert(!fake::allowed);}
  web.request(HTTP_POST,"/api/obs/reconnect");response(web,503);assert(!fake::allowed);
  fake::accepted=true;web.request(HTTP_POST,"/api/obs/reconnect");response(web,202);assert(fake::allowed);
  old=io::files.at(io::Active);applies=fake::applies;io::noSpace=true;
  web.request(HTTP_PUT,"/api/obs",clear);assert(response(web,500)["saved"]==false);
  assert(io::files.at(io::Active)==old&&fake::applies==applies);io::noSpace=false;
  std::cout<<"PASS PUT/password/save-vs-apply failures, reconnect, global permission and continuous stop\n";
  // Rename failure before commit, after commit, corrupt commit, unreadable commit.
  for(int mode=0;mode<4;++mode){
    io::reset();fake::reset();seed();Controller c;c.begin();c.tick(true);
    WebServer w;registerObsHttp(w,c,maintenance,outputs);applies=fake::applies;
    if(mode==0)io::failRename=true;else io::renameFailureMode=mode;
    w.request(HTTP_PUT,"/api/obs",clear);auto result=response(w,500);
    assert(result["saved"]==false&&result["error"]["code"]=="renameFailed");
    assert(fake::applies==applies&&!fake::allowed);
    c.tick(true);assert(!fake::allowed);
    w.request(HTTP_GET,"/api/obs");auto get=response(w,200);
    assert(get["applyState"]=="notRequested");
    if(mode<2){
      assert(get["storageState"]=="ready"&&get["settings"]["passwordConfigured"]==(mode==0));
      JsonDocument status;c.statusJson(status.to<JsonObject>());
      assert(status["blockedReason"]=="reapplyRequired"&&status["stopRequested"]==true&&status["connectionState"]!="Ready");
      int before=io::writes;w.request(HTTP_POST,"/api/obs/reconnect");response(w,202);
      assert(fake::allowed&&io::writes==before&&bool(fake::applied.password[0])==(mode==0));
    }else{
      assert(get["storageState"]==(mode==2?"corrupt":"ioError")&&get["settings"].isNull());
      w.request(HTTP_POST,"/api/obs/reconnect");response(w,409);assert(!fake::allowed);
    }
    io::intact();
  }
  io::reset();fake::reset();io::files[io::Active]="{";Controller broken;broken.begin();
  WebServer b;registerObsHttp(b,broken,maintenance,outputs);
  b.request(HTTP_PUT,"/api/obs",update);response(b,409);
  auto unknown=omit;unknown.insert(unknown.size()-1,",\"replaceInvalid\":true");
  b.request(HTTP_PUT,"/api/obs",unknown);assert(response(b,400)["error"]["code"]=="passwordRequired");
  auto replace=update;replace.insert(replace.size()-1,",\"replaceInvalid\":true");
  b.request(HTTP_PUT,"/api/obs",replace);response(b,200);assert(fake::allowed);
  b.request(HTTP_PUT,"/api/obs",omit);response(b,200);assert(!fake::applied.enabled);
  b.request(HTTP_POST,"/api/obs/reconnect");response(b,409);io::intact();
  std::cout<<"PASS rename uncertain outcomes/reload/no automatic apply/explicit recovery; Config sentinels intact\n";
}
