#include "model.h"
#include "json_wire.h"
#include "factory_reset.h"
#include "storage_probe.h"
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
  String e;probeFake::clear();
  CHECK(probeSettingsFile("/config.records")==StoragePresence::Absent);
  CHECK(loadConfig(e)&&configStorageState()==ConfigStorageState::Missing&&configOutputsAllowed());
  auto c=fixture();seed(c);const auto original=disk();
  CHECK(probeSettingsFile("/config.records")==StoragePresence::Present);
  probeFake::successErrno=ENOENT;CHECK(probeSettingsFile("/config.records")==StoragePresence::Present);probeFake::successErrno=0;
  for(int err:{EIO,ENOMEM,EINVAL,EBADMSG,ENOTDIR,EMFILE,0,9999}){
    probeFake::statErrors["/config.records"]=err;CHECK(probeSettingsFile("/config.records")==StoragePresence::Error);
  }
  probeFake::statErrors.clear();
  probeFake::directories.insert("/config.records");CHECK(probeSettingsFile("/config.records")==StoragePresence::Error);probeFake::directories.clear();
  probeFake::mountpoint="/wrong";CHECK(probeSettingsFile("/config.records")==StoragePresence::Error);probeFake::clear();
  probeFake::rootError=EIO;CHECK(probeSettingsFile("/config.records")==StoragePresence::Error);probeFake::clear();
  CHECK(probeSettingsFile("/../config.records")==StoragePresence::Error);
  std::cout<<"PASS SAFETY H3 probe: Present/Absent/Error, errno matrix, stale errno, directory, wrong root/path\n";

  // Each failure starts with a fully loaded, healthy active snapshot. Even
  // after the injected fault disappears, GET(defaults) must not authorize save.
  for(int fault=0;fault<7;++fault){
    probeFake::clear();storageFault::clearFaults();CHECK(loadConfig(e));
    if(fault==0)probeFake::statErrors["/config.records"]=EIO;
    if(fault==1)storageFault::openFailurePath="/config.records";
    if(fault==2)storageFault::readFailurePath="/config.records";
    if(fault==3)*storageFault::files.at("/config.records")="bad header\n";
    if(fault==4)storageFault::files.at("/config.records")->pop_back(); // valid header, damaged body
    if(fault==5)probeFake::mounted=false;
    if(fault==6)probeFake::registered=false;
    const auto preserved=disk();CHECK(!loadConfig(e)&&!configOutputsAllowed());
    probeFake::clear();storageFault::clearFaults();
    web.request("/api/config",HTTP_GET);CHECK(web.status==200);
    JsonDocument got,request;CHECK(!deserializeJson(got,web.body));
    request["schemaVersion"]=2;request["network"]=got["network"];
    CHECK(!beginConfigSave(json(request),e));CHECK(!saveWifiConfig("{\"ssid\":\"overwrite\",\"password\":\"\"}",e));
    CHECK(disk()==preserved&&!configOutputsAllowed());
    if(fault==0){storageFault::files.erase("/config.records");CHECK(!loadConfig(e)&&!configOutputsAllowed());}
    storageFault::files["/config.records"]=std::make_shared<std::string>(original);
    CHECK(loadConfig(e)&&configOutputsAllowed()&&config.chains[0].count==16);
    std::cout<<"PASS SAFETY H3 boot fault="<<fault<<": GET defaults cannot overwrite; bytes retained; only full load unlocks\n";
  }
  JsonDocument request,n;encodeNetwork(config,n);request["schemaVersion"]=2;request["network"]=n;
  probeFake::statErrors["/config.records"]=EIO;CHECK(!beginConfigSave(json(request),e));
  CHECK(configOutputsAllowed()&&config.chains[0].count==16&&disk()==original);
  probeFake::clear();CHECK(beginConfigSave(json(request),e)); // no boot lock from save-only failure
  CHECK(saveWifiConfig("{\"ssid\":\"recovered\",\"password\":\"\"}",e));
  std::cout<<"PASS SAFETY H3 save-only probe failure retains RAM; normal load recovery permits saving\n";

  for(const char* marker:{"/factory-reset.pending","/factory-reset.done"}){
    for(int fault=0;fault<5;++fault){
      storageFault::clearFaults();probeFake::clear();
      storageFault::files[marker]=std::make_shared<std::string>(fault==4?"BAD:1":"FR1:1");
      if(fault==0)probeFake::statErrors[marker]=EIO;
      if(fault==1)storageFault::openFailurePath=marker;
      if(fault==2)storageFault::readFailurePath=marker;
      if(fault==3)probeFake::registered=false;
      auto bytes=*storageFault::files.at(marker);auto count=storageFault::renames;
      CHECK(factoryResetBoot()==ResetBoot::Failed&&factoryResetBlocked());
      CHECK(*storageFault::files.at(marker)==bytes&&storageFault::renames==count);
      storageFault::files.erase(marker);
    }
  }
  probeFake::clear();storageFault::clearFaults();
  storageFault::files["/factory-reset.done"]=std::make_shared<std::string>("FR1:1");
  CHECK(factoryResetBoot()==ResetBoot::Complete&&!factoryResetBlocked());storageFault::files.erase("/factory-reset.done");
  storageFault::files["/factory-reset.tmp"]=std::make_shared<std::string>("FR1:1");CHECK(factoryResetBoot()==ResetBoot::Normal);
  storageFault::files.erase("/factory-reset.tmp");
  probeFake::mounted=false;probeFake::erased=false;CHECK(factoryResetBoot()==ResetBoot::Failed);
  probeFake::erased=true;probeFake::partitionReads=0;CHECK(factoryResetBoot()==ResetBoot::Normal);CHECK(probeFake::partitionReads==3);
  probeFake::partitionReads=0;probeFake::partitionReadFailure=1;CHECK(factoryResetBoot()==ResetBoot::Failed);
  probeFake::partitionReadFailure=-1;probeFake::partitionMissing=true;CHECK(factoryResetBoot()==ResetBoot::Failed);
  probeFake::clear();CHECK(factoryResetBoot()==ResetBoot::Normal&&!factoryResetBlocked());
  CHECK(loadConfig(e));
  std::cout<<"PASS SAFETY H3 pending/done stat-open-read-corrupt errors stop, markers retained, tmp ignored; erased initial boot/non-erased/read-error/missing partition\n";
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
  const char* names[]={"normal commit apply failure","Wi-Fi apply failure","Wi-Fi invalid input","Wi-Fi pending write failure","Wi-Fi success"};
  for(int scenario=0;scenario<5;++scenario){
    const bool wifi=scenario!=0, success=scenario==4, stop=scenario<2||success;
    Config c=fixture();c.chains[0].count=1;c.chains[0].actions[0].protocol=Protocol::Wait;c.chains[0].actions[0].delayMs=10000;
    seed(c);uint32_t t=wifi?0:stage(c);pendingWork();restartAt=0;
    const auto before=disk();const auto renames=storageFault::renames;
    storageFault::failAfterRename=scenario<2;
    storageFault::pendingWriteFailures=scenario==3?1:0;
    int responses=0;
    auto checkStop=[&]{
      CHECK(engine.activeCount()==(stop?0:1));
      CHECK(midiQueued(Transport::Ble)==!stop);CHECK(keyboardQueued(Transport::Ble)==!stop);
      CHECK(transportFake::discarded==(stop?1:0));
    };
    // Observe at send(), not merely after handler return: cancellation must
    // precede the HTTP response. Physical HID release can wait for transport.
    web.beforeSend=[&](int code){++responses;CHECK(code==(success?200:400));checkStop();};
    web.request(wifi?"/api/wifi":"/api/config/commit",wifi?HTTP_PUT:HTTP_POST,
      scenario==2?"{\"ssid\":42,\"password\":\"\"}":wifi?"{\"ssid\":\"after\",\"password\":\"\"}":"",t);
    web.beforeSend={};CHECK(responses==1);checkStop();
    CHECK(storageFault::injected==((scenario<2||scenario==3)?1:0));
    CHECK(configOutputsAllowed()==(scenario>=2));
    CHECK(storageFault::renames==renames+(stop?1:0));
    CHECK(restartAt==(success?millis()+750:0));
    JsonDocument response;CHECK(!deserializeJson(response,web.body));
    CHECK(response["ok"].as<bool>()==success);
    const char* expected=success?"Saved. Restarting.":scenario<2?"Committed settings read failed; restart required":scenario==2?"Invalid network settings":"LittleFS write failed";
    CHECK(response["message"]==expected);
    if(!stop){CHECK(disk()==before);CHECK(config.chains[0].count==1&&String(config.ssid)=="before");}
    if(success){CHECK(String(config.ssid)=="after");CHECK(disk()!=before);}
    transportFake::writable=true;for(int i=0;i<4;++i)backendsTick(0);
    CHECK(!transportFake::hid.empty());
    bool released=transportFake::hid.back().modifiers==0&&std::all_of(std::begin(transportFake::hid.back().keys),std::end(transportFake::hid.back().keys),[](auto k){return k==0;});
    CHECK(released==stop);CHECK(transportFake::midi.empty()==stop);
    std::cout<<"PASS SAFETY H2 "<<names[scenario]<<": HTTP"<<web.status<<" outputsAllowed="<<configOutputsAllowed()<<" jobs="<<unsigned(engine.activeCount())
      <<" midiSentAfterResponse="<<transportFake::midi.size()<<" finalHidReleased="<<released<<" inputDiscard="<<transportFake::discarded<<" restartAt="<<restartAt<<" checkedBeforeResponse=1\n";
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
int main(int argc,char** argv){try{
  if(argc==2&&std::string(argv[1])=="--blank-boot"){
    // Separate process: mounted cache and boot-read latch start at power-on values.
    probeFake::clear();probeFake::mounted=false;probeFake::erased=true;
    CHECK(factoryResetBoot()==ResetBoot::Normal);String error;
    CHECK(loadConfig(error)&&configStorageState()==ConfigStorageState::Missing&&configOutputsAllowed());
    CHECK(storageFault::files.empty()&&config.chains[0].count==0);
    std::cout<<"PASS SAFETY H3 fresh process: erased unmountable settings -> normal reset flow -> initial Config mount/format\n";
    return 0;
  }
  registerHandlers();h3();h4();h1();h2();
  std::cout<<"PASS H2/H3 safety regression; H1/H4 characterization reproduced (UNFIXED)\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
