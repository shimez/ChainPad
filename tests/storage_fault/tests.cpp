#include "model.h"
#include "json_wire.h"
#include "factory_reset.h"
#include "engine.h"
#include "backend_internal.h"
#include "rotation_runtime.h"
#include "LittleFS.h"
#include "http_adapter.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>
using namespace chimera;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #x);}while(0)
namespace transportFake {
bool writable=true;std::vector<KeyboardReport> hid;std::vector<int> midi;
int discarded=0;
}
namespace chimera {
void transportsBegin(){}void transportsTick(){}
bool midiReady(Transport){return true;}bool keyboardReady(Transport){return true;}
uint32_t midiEpoch(Transport){return 1;}uint32_t keyboardEpoch(Transport){return 1;}
bool midiWrite(Transport,uint8_t s,uint8_t,uint8_t){if(!transportFake::writable)return false;transportFake::midi.push_back(s);return true;}
bool keyboardWrite(Transport,const KeyboardReport& r){if(!transportFake::writable)return false;transportFake::hid.push_back(r);return true;}
SendResult oscDispatch(const Action&){return SendResult::Accepted;}
SendResult oscRotationDispatch(const RotationOutput&,const RotationMappedValue&){return SendResult::Accepted;}
void discardInputs(){++transportFake::discarded;}
}
HttpAdapter web;String bootMessage;uint32_t restartAt=0;
void diagnosticCheckpoint(const char*){}
#include "handlers.inc" // Verbatim main.cpp handlers, responses, reset guard, panic.

std::string disk(){return *storageFault::files.at("/config.records");}
String json(JsonDocument& d){String s;serializeSettingsJson(d,s);return s;}
uint32_t stage(const Config& c){
  String error;JsonDocument d,n;encodeNetwork(c,n);d["schemaVersion"]=2;d["network"]=n;
  auto token=beginConfigSave(json(d),error);CHECK(token);
  for(uint8_t i=0;i<28;++i){encodeChain(c.chains[i],i,d);CHECK(stageConfigChain(token,i,json(d),error));}
  encodeRotation(c.encoderRotation,d,true);CHECK(stageConfigRotation(token,json(d),error));return token;
}
void seed(const Config& c){
  jsonFault::arm("",0);storageFault::clearFaults();String e;
  auto t=stage(c);CHECK(commitConfigSave(t,e));CHECK(loadConfig(e));
}
Config fixture(int variant=0){
  Config c;strcpy(c.ssid,"before");c.oscPort=9100;
  c.chains[0].count=16;
  for(int i=0;i<16;++i){auto& a=c.chains[0].actions[i];
    if(i<variant){a.protocol=Protocol::Wait;a.delayMs=1000;}
    else {a.protocol=Protocol::Osc;a.oscType=OscType::String;strcpy(a.address,"/fault/test");strcpy(a.stringValue,"a sufficiently long shared value");}
  }
  c.encoderRotation.outputCount=16;
  for(int i=0;i<16;++i){auto& o=c.encoderRotation.outputs[i];
    if(i<variant){o.kind=RotationOutputKind::MidiCC;o.transport=Transport::Ble;o.channel=1;o.number=i;}
    else {o.kind=RotationOutputKind::OscInt;strcpy(o.address,"/fault/rotation");}
  }
  return c;
}
void h3(){
  auto c=fixture();seed(c);const auto before=disk();String e;
  storageFault::failExistsPath="/config.records";storageFault::existsFailures=1;
  CHECK(loadConfig(e));CHECK(storageFault::injected==1);CHECK(disk()==before);
  CHECK(configStorageState()==ConfigStorageState::Missing&&configOutputsAllowed());
  CHECK(config.chains[0].count==0);
  Config defaults=config;jsonFault::arm("",0);auto t=stage(defaults);CHECK(commitConfigSave(t,e));CHECK(disk()!=before);
  std::cout<<"REPRO H3 active present -> exists-open failure -> Missing, defaults, outputsAllowed=1; subsequent save overwrites active\n";
  seed(c);storageFault::activeReadFailures=1;CHECK(!loadConfig(e));CHECK(configStorageState()==ConfigStorageState::IoError);CHECK(!configOutputsAllowed());
  std::cout<<"SAFE H3 control: exists succeeds, explicit active open fails -> IoError, outputsAllowed=0\n";
  storageFault::files["/factory-reset.pending"]=std::make_shared<std::string>("FR1:1");
  storageFault::clearFaults();storageFault::failExistsPath="/factory-reset.pending";storageFault::existsFailures=1;
  CHECK(factoryResetBoot()==ResetBoot::Normal);CHECK(storageFault::injected==1);
  CHECK(!factoryResetBlocked()&&storageFault::files.count("/factory-reset.pending"));
  storageFault::files.erase("/factory-reset.pending");
  std::cout<<"REPRO H3 reset journal present, NVS intent absent -> exists-open failure -> Normal, journal remains unread\n";
}
void h1(){
  Config old=fixture(),next=old;next.oscPort=9200;strcpy(next.ssid,"new-on-disk");
  for(bool restart:{true,false}){
    seed(old);auto previous=disk();auto t=stage(next);auto expected=*storageFault::files.at("/pending.records");storageFault::failAfterRename=true;
    web.request("/api/config/commit",HTTP_POST,"",t);
    CHECK(web.status==400&&storageFault::injected==1);CHECK(disk()!=previous);
    auto committed=disk();CHECK(committed==expected);CHECK(committed.find("new-on-disk")!=std::string::npos);
    CHECK(config.chains[0].count==0&&config.ssid[0]==0&&!configOutputsAllowed());
    if(restart){String e;CHECK(loadConfig(e));CHECK(config.oscPort==9200&&config.chains[0].count==16);CHECK(disk()==committed);}
    else {
      web.request("/api/config",HTTP_GET);CHECK(web.status==200);
      JsonDocument d;CHECK(!deserializeJson(d,web.body));Config fromUI;String e;CHECK(decodeConfig(d.as<JsonVariantConst>(),fromUI,e));
      CHECK(fromUI.chains[0].count==0);t=stage(fromUI);web.request("/api/config/commit",HTTP_POST,"",t);
      CHECK(web.status==200&&disk()!=committed);CHECK(loadConfig(e));CHECK(config.chains[0].count==0&&config.ssid[0]==0);
    }
    std::cout<<"REPRO H1 post-rename active-open failure: HTTP400, disk=new, RAM=defaults, outputsAllowed=0; "
             <<(restart?"loadConfig (reboot model) recovers new":"actual GET + subsequent save destroys healthy new config")<<"\n";
  }
}
void pendingWork(){
  engine.cancelAll();transportFake::writable=true;backendsPanic();for(int i=0;i<4;++i)backendsTick(0);
  Action down;down.protocol=Protocol::Keyboard;down.transport=Transport::Ble;down.usage=4;down.keyMessage=KeyMessage::Down;
  CHECK(keyboardDispatch(down)==SendResult::Accepted);CHECK(!transportFake::hid.empty()&&transportFake::hid.back().keys[0]==4);
  transportFake::writable=false;down.usage=5;CHECK(keyboardDispatch(down)==SendResult::Accepted);
  Action note;note.protocol=Protocol::Midi;note.transport=Transport::Ble;CHECK(midiDispatch(note)==SendResult::Accepted);
  CHECK(engine.trigger({0,millis()}));CHECK(engine.activeCount()==1);
  CHECK(midiQueued(Transport::Ble)&&keyboardQueued(Transport::Ble));
  transportFake::hid.clear();transportFake::midi.clear();transportFake::discarded=0;
}
void h2(){
  for(bool wifi:{false,true}){
    Config c=fixture();c.chains[0].count=1;c.chains[0].actions[0].protocol=Protocol::Wait;c.chains[0].actions[0].delayMs=10000;
    seed(c);uint32_t t=wifi?0:stage(c);pendingWork();storageFault::failAfterRename=true;
    web.request(wifi?"/api/wifi":"/api/config/commit",wifi?HTTP_PUT:HTTP_POST,wifi?"{\"ssid\":\"after\",\"password\":\"\"}":"",t);
    CHECK(storageFault::injected==1&&web.status==400&&!configOutputsAllowed());
    CHECK(engine.activeCount()==(wifi?1:0));CHECK(midiQueued(Transport::Ble)==wifi);CHECK(keyboardQueued(Transport::Ble)==wifi);
    CHECK(transportFake::discarded==(wifi?0:1));
    transportFake::writable=true;for(int i=0;i<4;++i)backendsTick(0);
    CHECK(!transportFake::hid.empty());
    bool released=std::all_of(std::begin(transportFake::hid.back().keys),std::end(transportFake::hid.back().keys),[](auto k){return k==0;});
    CHECK(released==!wifi);CHECK(transportFake::midi.empty()==!wifi);
    std::cout<<(wifi?"REPRO":"SAFE")<<" H2 "<<(wifi?"Wi-Fi PUT":"normal commit")<<": HTTP400 outputsAllowed=0 jobs="<<unsigned(engine.activeCount())
      <<" midiSentAfterFailure="<<transportFake::midi.size()<<" finalHidReleased="<<released<<" inputDiscard="<<transportFake::discarded<<"\n";
  }
  engine.cancelAll();backendsPanic();
}
void h4(){
  for(const char* kind:{"chain","rotation"}){
    bool saveRepro=false,getRepro=false,rejected=false;
    for(int variant=0;variant<=16 && (!saveRepro||!getRepro||!rejected);++variant){
      const auto c=fixture(variant);seed(c);jsonFault::arm(kind,0);web.request("/api/config",HTTP_GET);int allocations=jsonFault::allocationCount;
      CHECK(web.status==200&&!jsonFault::overflow&&jsonFault::failures==0);
      JsonDocument baseline;CHECK(!deserializeJson(baseline,web.body));Config intact;String error;
      CHECK(decodeConfig(baseline.as<JsonVariantConst>(),intact,error));
      CHECK(intact.chains[0].count==16&&intact.encoderRotation.outputCount==16);
      for(int at=1;at<=allocations && (!saveRepro||!getRepro||!rejected);++at){
        seed(c);auto before=disk();jsonFault::arm(kind,at);
        web.request("/api/wifi",HTTP_PUT,"{\"ssid\":\"oom-save\",\"password\":\"\"}");
        CHECK(jsonFault::failures==1&&jsonFault::overflow);
        JsonDocument fragment;CHECK(!deserializeJson(fragment,jsonFault::wire));
        const int remaining=std::string(kind)=="chain"?fragment["actions"].size():fragment["rotationValue"]["outputs"].size();
        if(web.status==200 && remaining>0 && remaining<16 && !saveRepro){
          CHECK((std::string(kind)=="chain"?config.chains[0].count:config.encoderRotation.outputCount)==remaining);
          String e;CHECK(loadConfig(e));CHECK((std::string(kind)=="chain"?config.chains[0].count:config.encoderRotation.outputCount)==remaining);
          saveRepro=true;std::cout<<"REPRO H4 Wi-Fi "<<kind<<" variant="<<variant<<" allocation="<<at<<" allocationBytes="<<jsonFault::sizes.at(at-1)<<" overflow=1 validJSON=1 HTTP200 count=16->"<<remaining<<" persisted after reload\n";
        }
        if(web.status==400&&remaining>0&&!rejected){CHECK(disk()==before);rejected=true;std::cout<<"SAFE H4 Wi-Fi "<<kind<<" variant="<<variant<<" allocation="<<at<<" overflow=1 validJSON=1 partialCount="<<remaining<<" schema rejects, active unchanged\n";}
        seed(c);jsonFault::arm(kind,at);web.request("/api/config",HTTP_GET);
        CHECK(jsonFault::failures==1&&jsonFault::overflow&&web.status==200);
        JsonDocument full;CHECK(!deserializeJson(full,web.body));Config received;String e;
        bool valid=decodeConfig(full.as<JsonVariantConst>(),received,e);
        int count=std::string(kind)=="chain"?received.chains[0].count:received.encoderRotation.outputCount;
        if(valid&&count>0&&count<16&&!getRepro){getRepro=true;std::cout<<"REPRO H4 GET "<<kind<<" variant="<<variant<<" allocation="<<at<<" overflow=1 validJSON=1 schemaValid=1 HTTP200 count=16->"<<count<<"\n";}
      }
    }
    CHECK(saveRepro&&getRepro&&rejected);
  }
  jsonFault::arm("",0);
}
int main(){try{
  registerHandlers();h3();h4();h1();h2();
  std::cout<<"PASS characterization: H1-H4 reproduced; NOT a post-fix safety pass\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
