#include "model.h"
#include "json_wire.h"
#include "factory_reset.h"
#include "storage_probe.h"
#include "engine.h"
#include "backend_internal.h"
#include "rotation_runtime.h"
#include "rotation_sender.h"
#include "LittleFS.h"
#include "reset_sdk.h"
#include "http_adapter.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <cmath>
#include <new>
namespace scratchFault {size_t size=0;int failures=0;}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
  if(scratchFault::size==size){++scratchFault::failures;return nullptr;}
  try{return ::operator new(size);}catch(...){return nullptr;}
}
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
bool consumerReady(Transport){return true;}
uint32_t consumerEpoch(Transport){return 1;}
bool consumerWrite(Transport,uint16_t){return transportFake::writable;}
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
  jsonFault::arm("",0);stringFault::arm();storageFault::clearFaults();String e;
  scratchFault::size=0;scratchFault::failures=0;
  if(configCommittedApplyFailed())CHECK(loadConfig(e));
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
void pendingWork();
void h1(){
  Config old=fixture(6),next=old;next.oscPort=9200;strcpy(next.ssid,"new-on-disk");
  const char* names[]={"open","header","first-chain","middle-chain","last-chain","rotation","last-newline","trailing-data","chain-scratch","rotation-scratch","JSON-allocation"};
  for(bool wifi:{false,true})for(int fault=0;fault<11;++fault){
    seed(old);auto previous=disk();auto t=stage(next);std::string expected;
    storageFault::beforeRename=[&]{expected=*storageFault::files.at("/pending.records");};
    storageFault::afterRename=[&]{
      if(fault==0)storageFault::activeReadFailures=1;
      else if(fault<7){
        std::vector<size_t> records{0};for(size_t i=0;i<expected.size();++i)if(expected[i]=='\n')records.push_back(i+1);
        CHECK(records.size()==31);
        const int record=fault==1?0:fault==2?1:fault==3?14:fault==4?28:29;
        storageFault::activeReadLimit=fault==6?expected.size()-1:records[record]+3;
      }else if(fault==7)storageFault::trailingActive=true;
      else if(fault==8)scratchFault::size=sizeof(Chain);
      else if(fault==9)scratchFault::size=sizeof(EncoderRotationSettings);
      else jsonFault::arm("all",1);
    };
    pendingWork();rotationPublish(1);CHECK(rotationPendingCount()>0);
    restartAt=0;const int renames=storageFault::renames;
    web.beforeSend=[&](int code){CHECK(code==400&&configCommittedApplyFailed()&&!configOutputsAllowed());CHECK(engine.activeCount()==0&&rotationPendingCount()==0);CHECK(!midiQueued(Transport::Ble)&&!keyboardQueued(Transport::Ble)&&transportFake::discarded==1);};
    web.request(wifi?"/api/wifi":"/api/config/commit",wifi?HTTP_PUT:HTTP_POST,wifi?"{\"ssid\":\"new-on-disk\",\"password\":\"\"}":"",t);
    web.beforeSend={};
    // Storage commit and failed activation are independent assertions.
    CHECK(storageFault::renames==renames+1&&!storageFault::files.count("/pending.records"));
    CHECK(!expected.empty()&&disk()==expected&&disk()!=previous);
    CHECK(web.status==400&&web.body.find("saved to storage")!=std::string::npos&&web.body.find("Restart")!=std::string::npos);
    CHECK(config.chains[0].count==0&&config.ssid[0]==0&&restartAt==0);
    CHECK(fault<8?storageFault::injected>0:fault<10?scratchFault::failures>0:jsonFault::failures>0);
    jsonFault::arm("",0);scratchFault::size=0;storageFault::clearFaults();
    CHECK(configCommittedApplyFailed()); // Removing the fault is not recovery.
    transportFake::writable=true;for(int i=0;i<4;++i)backendsTick(0);
    CHECK(transportFake::midi.empty()&&!transportFake::hid.empty());
    const auto released=transportFake::hid.back();CHECK(released.modifiers==0&&std::all_of(std::begin(released.keys),std::end(released.keys),[](uint8_t key){return key==0;}));
    for(const char* path:{"/api/config","/api/wifi"}){web.request(path,HTTP_GET);CHECK(web.status==503&&!web.chunked&&web.body.find("schemaVersion")==std::string::npos&&web.body.find("ssid")==std::string::npos);}
    JsonDocument request,network;CHECK(encodeNetwork(next,network));request["schemaVersion"]=2;request["network"]=network;request["replaceUnsupported"]=true;
    web.request("/api/config/begin",HTTP_POST,json(request).c_str());CHECK(web.status==400);
    web.request("/api/config/chain",HTTP_PUT,"{}",t);CHECK(web.status==400);
    web.request("/api/config/rotation",HTTP_PUT,"{}",t);CHECK(web.status==400);
    web.request("/api/config/commit",HTTP_POST,"",t);CHECK(web.status==400);
    web.request("/api/wifi",HTTP_PUT,"{\"ssid\":\"overwrite\",\"password\":\"\"}");CHECK(web.status==400);
    CHECK(disk()==expected&&storageFault::renames==renames+1&&restartAt==0&&!configOutputsAllowed());
    String e;
    if(fault==0){
      // No new persistent provenance is introduced. Keep the H1 latch through
      // same-process Missing/Unsupported and through failed recovery scans.
      storageFault::files.erase("/config.records");CHECK(!loadConfig(e)&&configCommittedApplyFailed());
      storageFault::files["/config.records"]=std::make_shared<std::string>("{\"storageVersion\":3}\n");
      CHECK(!loadConfig(e)&&configStorageState()==ConfigStorageState::UnsupportedVersion&&configCommittedApplyFailed());
      CHECK(!beginConfigSave(json(request),e));
      storageFault::files["/config.records"]=std::make_shared<std::string>(expected);
      storageFault::readFailurePath="/config.records";CHECK(!loadConfig(e)&&configCommittedApplyFailed());storageFault::clearFaults();
      storageFault::failActiveOpenAt=3; // header inspection + full validation succeed; apply open fails
      CHECK(!loadConfig(e)&&storageFault::injected==1&&configCommittedApplyFailed()&&!configOutputsAllowed());storageFault::clearFaults();
      web.request("/api/restart",HTTP_POST);CHECK(web.status==200&&restartAt==millis()+750&&configCommittedApplyFailed());restartAt=0;
      web.request("/api/factory-reset",HTTP_GET);CHECK(web.status==200);
      auto commits=resetFault::commits;web.request("/api/factory-reset",HTTP_POST,"{\"confirm\":\"factory-reset\"}");
      CHECK(web.status==202&&resetFault::intent==0&&resetFault::commits==commits+1&&factoryResetBlocked());
      // Test acceptance only; destructive reset/retry flow is in platform suite.
      resetFault::intent=-1;CHECK(factoryResetBoot()==ResetBoot::Normal);restartAt=0;
      std::ofstream(".pio/storage-fault-tests/h1-active.records")<<expected;
    }
    CHECK(loadConfig(e)&&!configCommittedApplyFailed()&&configOutputsAllowed());
    CHECK(config.chains[0].count==16&&String(config.ssid)=="new-on-disk"&&disk()==expected);
    web.request("/api/config",HTTP_GET);CHECK(web.status==200&&web.finished);
    web.request("/api/wifi",HTTP_GET);CHECK(web.status==200&&web.body.find("new-on-disk")!=std::string::npos);
    std::cout<<"PASS SAFETY H1 "<<(wifi?"Wi-Fi ":"commit ")<<names[fault]<<": rename=1/new bytes retained; pre-response panic; GET503/write lock; full load unlocks\n";
  }
  // Pre-commit failures must never claim the new settings were saved.
  for(bool rename:{false,true}){seed(old);auto previous=disk();auto t=stage(next);auto renames=storageFault::renames;
    if(rename)storageFault::renameFailure=true;else storageFault::files.at("/pending.records")->pop_back();
    web.request("/api/config/commit",HTTP_POST,"",t);CHECK(web.status==400&&!configCommittedApplyFailed()&&configOutputsAllowed());
    CHECK(disk()==previous&&storageFault::renames==renames&&web.body.find("saved to storage")==std::string::npos);storageFault::clearFaults();}
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
    const char* expected=success?"Saved. Restarting.":scenario<2?"Settings saved to storage, but runtime apply failed. Outputs disabled. Do not resave; Restart required.":scenario==2?"Invalid network settings":"LittleFS write failed";
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
  auto ram=[](){JsonDocument d;CHECK(encodeConfig(config,d));return std::string(json(d).c_str());};
  int cases=0;
  // All allocation boundaries, including optional holdMs/transport, string
  // storage, nested fields and pool expansion. Include the old 16->15 fixtures.
  for(int variant:{0,4,6,16,17}){
    auto c=fixture(variant==17?0:variant);
    if(variant==17){
      for(int i=0;i<16;++i){auto& a=c.chains[0].actions[i];a.protocol=Protocol::Midi;a.message=MidiMessage::NoteOnOff;a.transport=Transport::Ble;a.channel=1;a.number=60;a.value=100;a.delayMs=777;
        auto& o=c.encoderRotation.outputs[i];o.kind=RotationOutputKind::OscFloat;o.range.floating={-0.0f,0.123456789f};}
    }
    seed(c);const auto before=disk(),snapshot=ram();
    for(const char* kind:{"network","chain","rotation"}){
      jsonFault::arm(kind,0);web.request("/api/config",HTTP_GET);int allocations=jsonFault::allocationCount;
      CHECK(web.status==200&&web.finished&&!web.stopped);
      for(bool persistent:{false,true})for(int at=1;at<=allocations;++at){
        for(int pass:{1,2}){
          stringFault::arm();jsonFault::arm(kind,at,pass,persistent);web.request("/api/config",HTTP_GET);
          CHECK(jsonFault::failures>=1);jsonFault::arm("",0);
          CHECK(disk()==before&&ram()==snapshot);
          if(pass==1){CHECK(web.status==500&&!web.chunked&&web.body.find("schemaVersion")==std::string::npos);}
          else {CHECK(web.status==200&&web.stopped&&!web.finished);JsonDocument d;CHECK(deserializeJson(d,web.body));
            if(variant==4&&at==1&&std::string(kind)=="chain"&&!persistent){std::ofstream(".pio/storage-fault-tests/get-aborted.json")<<web.body;}}
          ++cases;
        }
        // First encode and stage's second encode must both propagate failure.
        for(int invocation:{1,2}){
          jsonFault::arm("",0);stringFault::arm();const auto oldToken=stage(c);
          auto renames=storageFault::renames;restartAt=0;transportFake::discarded=0;
          jsonFault::arm(kind,at,invocation,persistent);
          web.request("/api/wifi",HTTP_PUT,"{\"ssid\":\"oom-save\",\"password\":\"\"}");
          CHECK(jsonFault::failures>=1);jsonFault::arm("",0);
          CHECK(web.status==400&&restartAt==0&&transportFake::discarded==0);
          CHECK(disk()==before&&ram()==snapshot&&storageFault::renames==renames&&configOutputsAllowed());
          String error;CHECK(!commitConfigSave(oldToken,error)&&!commitConfigSave(oldToken+1,error));++cases;
        }
      }
    }
  }
  jsonFault::arm("",0);stringFault::arm();
  auto c=fixture(4);seed(c);const auto before=disk(),snapshot=ram();
  // Reserve/concat failures independently target every one of the 60 GET
  // record serializations, plus all 30 Wi-Fi rewrite records.
  for(bool wifi:{false,true})for(int record=1;record<=(wifi?30:60);++record){
    for(int failure:{0,1,7})for(bool persistent:{false,true}){
      stringFault::arm();auto token=stage(c);auto renames=storageFault::renames;restartAt=0;transportFake::discarded=0;
      stringFault::arm(record,failure==0,failure,persistent);
      web.request(wifi?"/api/wifi":"/api/config",wifi?HTTP_PUT:HTTP_GET,wifi?"{\"ssid\":\"new\",\"password\":\"\"}":"");
      CHECK(stringFault::failures>=1);stringFault::arm();
      CHECK(disk()==before&&ram()==snapshot&&storageFault::renames==renames);
      if(wifi){CHECK(web.status==400&&restartAt==0&&transportFake::discarded==0);String e;CHECK(!commitConfigSave(token,e)&&!commitConfigSave(token+1,e));}
      else if(record<=30)CHECK(web.status==500&&!web.chunked);
      else CHECK(web.status==200&&web.stopped&&!web.finished);
      ++cases;
    }
  }
  // Last byte and short multi-byte escape writes: failure is sticky even if
  // the next sink call would succeed. No partial String escapes the helper.
  JsonDocument d;d["control"]="a\x01z";String text;auto size=measureSettingsJson(d);
  CHECK(checkedSettingsJson(d,text));CHECK(text.length()==size);
  stringFault::arm(1,false,int(size)-5);CHECK(!checkedSettingsJson(d,text)&&text.isEmpty());stringFault::arm();
  struct ShortSink {int calls=0;size_t write(const uint8_t*,size_t n){++calls;return n>1?n-1:0;}} sink;
  SettingsJsonWriter<ShortSink> writer{sink};CHECK(!writer.write(uint8_t(1))&&writer.failed&&writer.bytes==5);
  CHECK(!writer.write(uint8_t('x'))&&sink.calls==1);
  // Full encode's intermediate copies/adds must report every allocation fault.
  jsonFault::arm("all",0);CHECK(encodeConfig(c,d));int allocations=jsonFault::calls;jsonFault::arm("",0);
  for(int at=1;at<=allocations;++at){jsonFault::arm("all",at);CHECK(!encodeConfig(c,d));CHECK(jsonFault::failures==1);jsonFault::arm("",0);++cases;}
  CHECK(ram()==snapshot&&disk()==before);
  // Exercise the actual saveWifiConfig prefix: network, credential member
  // replacement, request deep-copy. Stop injection before parse/commit work.
  int prefixAllocations=0;
  stringFault::beforeReserve=[&]{if(jsonFault::enabled){prefixAllocations=jsonFault::calls;jsonFault::enabled=false;}};
  jsonFault::arm("request",0);
  web.request("/api/wifi",HTTP_PUT,"{\"ssid\":\"copied credentials\",\"password\":\"long copied password\"}");
  CHECK(web.status==200&&prefixAllocations>0);jsonFault::arm("",0);seed(c);
  for(bool persistent:{false,true})for(int at=1;at<=prefixAllocations;++at){
    auto token=stage(c);auto renames=storageFault::renames;restartAt=0;transportFake::discarded=0;
    stringFault::beforeReserve=[] {jsonFault::enabled=false;};jsonFault::arm("request",at,1,persistent);
    web.request("/api/wifi",HTTP_PUT,"{\"ssid\":\"copied credentials\",\"password\":\"long copied password\"}");
    CHECK(jsonFault::failures>=1);jsonFault::arm("",0);stringFault::arm();
    CHECK(web.status==400&&restartAt==0&&transportFake::discarded==0&&storageFault::renames==renames);
    CHECK(disk()==before&&ram()==snapshot);String e;CHECK(!commitConfigSave(token,e)&&!commitConfigSave(token+1,e));++cases;
  }
  std::cout<<"PASS SAFETY H4 "<<cases<<" cases: encode/pools/strings/optional fields/copies; Wi-Fi byte+RAM retention, no rename/restart/panic, expired transactions; GET preflight500/send abort; sticky short writes\n";
  web.request("/api/config",HTTP_GET);CHECK(web.status==200&&web.finished&&!web.stopped);
  std::ofstream(".pio/storage-fault-tests/get-complete.json")<<web.body;
  String error;JsonDocument full;CHECK(!deserializeJson(full,web.body));Config received;
  CHECK(full["network"]["password"].isUnbound());full["network"]["password"]="";
  CHECK(decodeConfig(full.as<JsonVariantConst>(),received,error));
  CHECK(received.chains[0].count==16&&received.encoderRotation.outputCount==16);
  // Timing includes actual checked GET handler, excludes file output/decoding.
  using Clock=std::chrono::steady_clock;double headerUs=0;auto start=Clock::now();
  web.beforeSend=[&](int code){CHECK(code==200);headerUs+=std::chrono::duration<double,std::micro>(Clock::now()-start).count();};
  double totalUs=0;for(int i=0;i<100;++i){start=Clock::now();web.request("/api/config",HTTP_GET);totalUs+=std::chrono::duration<double,std::micro>(Clock::now()-start).count();}
  web.beforeSend={};std::cout<<"HOST GET timing n=100: header_us="<<headerUs/100<<" total_us="<<totalUs/100<<" (host only, no transport/ESP timing inference)\n";
  auto maximum=fixture();
  for(int i=0;i<16;++i){auto& a=maximum.chains[0].actions[i];
    memset(a.address,'a',sizeof(a.address)-1);a.address[0]='/';a.address[sizeof(a.address)-1]=0;
    memset(a.stringValue,'s',sizeof(a.stringValue)-1);a.stringValue[0]=1;a.stringValue[sizeof(a.stringValue)-1]=0;
    auto& o=maximum.encoderRotation.outputs[i];o.kind=RotationOutputKind::OscFloat;
    o.range.floating=i%2?RotationFloatRange{-0.0f,0.1f}:RotationFloatRange{-std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};}
  seed(maximum);web.request("/api/config",HTTP_GET);CHECK(web.status==200&&web.finished);
  CHECK(web.body.find("\\u0001")!=std::string::npos);CHECK(!deserializeJson(full,web.body));
  CHECK(full["network"]["password"].isUnbound());full["network"]["password"]="";
  CHECK(decodeConfig(full.as<JsonVariantConst>(),received,error));
  for(int i=0;i<16;++i){CHECK(!strcmp(received.chains[0].actions[i].address,maximum.chains[0].actions[i].address));
    CHECK(!strcmp(received.chains[0].actions[i].stringValue,maximum.chains[0].actions[i].stringValue));
    CHECK(received.encoderRotation.outputs[i].range.floating.start==maximum.encoderRotation.outputs[i].range.floating.start);
    CHECK(received.encoderRotation.outputs[i].range.floating.end==maximum.encoderRotation.outputs[i].range.floating.end);}
  CHECK(std::signbit(received.encoderRotation.outputs[1].range.floating.start));
  restartAt=0;web.request("/api/wifi",HTTP_PUT,"{\"ssid\":\"max-preserved\",\"password\":\"\"}");
  CHECK(web.status==200&&restartAt==millis()+750&&loadConfig(error));
  CHECK(!strcmp(config.chains[0].actions[15].stringValue,maximum.chains[0].actions[15].stringValue));
  CHECK(config.encoderRotation.outputs[0].range.floating.end==std::numeric_limits<float>::max());
  CHECK(std::signbit(config.encoderRotation.outputs[1].range.floating.start));
  std::cout<<"PASS SAFETY H4 normal max16+16/long strings/C0 escaping/float endpoints/negative zero/Wi-Fi restart/save-reload\n";
}
void wifiPrivacy(){
  probeFake::clear();String error;CHECK(loadConfig(error));registerHandlers();
  auto c=fixture();strcpy(c.password,"private-wifi-secret");seed(c);
  auto get=[&](bool configured){
    for(const char* path:{"/api/config","/api/wifi"}){
      web.request(path,HTTP_GET);CHECK(web.status==200);
      CHECK(web.body.find("private-wifi-secret")==std::string::npos);
      JsonDocument d;CHECK(!deserializeJson(d,web.body));
      auto n=std::string(path)=="/api/config"?d["network"].as<JsonVariantConst>():d.as<JsonVariantConst>();
      CHECK(n["password"].isUnbound()&&n["passwordConfigured"].is<bool>()&&n["passwordConfigured"]==configured);
      CHECK(n["ssid"]==config.ssid);
    }
  };
  auto begin=[&](const char* field){
    std::string body="{\"schemaVersion\":2,\"network\":{\"ssid\":\"edited\",\"oscHost\":\"10.1.2.3\",\"oscPort\":9012";
    body+=field;body+="}}";web.request("/api/config/begin",HTTP_POST,body.c_str());
    if(web.status!=200)return uint32_t(0);
    JsonDocument d;CHECK(!deserializeJson(d,web.body));return d["token"].as<uint32_t>();
  };
  auto finish=[&](uint32_t t){
    JsonDocument d;
    for(uint8_t i=0;i<28;++i){CHECK(encodeChain(c.chains[i],i,d));CHECK(stageConfigChain(t,i,json(d),error));}
    CHECK(encodeRotation(c.encoderRotation,d,true));CHECK(stageConfigRotation(t,json(d),error));
    CHECK(commitConfigSave(t,error));
  };
  get(true);
  JsonDocument d;CHECK(encodeNetwork(config,d));CHECK(d["password"]=="private-wifi-secret");
  d.remove("password");NetworkSettings n;CHECK(!decodeNetwork(d.as<JsonVariantConst>(),n,error));
  for(bool wifi:{false,true}){
    seed(c);const auto before=disk();
    for(const char* bad:{"null","123","false","[]","{}","\"embedded\\u0000nul\""}){
      std::string field=",\"password\":";field+=bad;
      if(wifi){std::string body="{\"ssid\":\"bad\"";body+=field;body+="}";web.request("/api/wifi",HTTP_PUT,body.c_str());}
      else CHECK(!begin(field.c_str()));
      CHECK(web.status==400&&disk()==before&&String(config.password)=="private-wifi-secret");
    }
    for(const char* value:{"",",\"password\":\"replacement\"", "",",\"password\":\"\"", "",",\"password\":\"replacement\""}){
      const String expected=String(value).isEmpty()?String(config.password):String(value)==",\"password\":\"\""?String(""):String("replacement");
      if(wifi){std::string body="{\"ssid\":\"edited\"";body+=value;body+="}";web.request("/api/wifi",HTTP_PUT,body.c_str());CHECK(web.status==200);}
      else {auto t=begin(value);CHECK(t);finish(t);}
      CHECK(String(config.password)==expected);CHECK(loadConfig(error)&&String(config.password)==expected);
      get(!expected.isEmpty());CHECK(config.chains[0].count==16&&config.encoderRotation.outputCount==16);
    }
  }
  // The header snapshots the current active password at begin, not at commit.
  seed(c);auto t=begin("");CHECK(t);strcpy(config.password,"later-ram-value");
  c.chains[0].actions[0].intValue=123;c.chains[0].actions[0].oscType=OscType::Int;finish(t);
  CHECK(String(config.password)=="private-wifi-secret"&&config.oscPort==9012&&config.chains[0].actions[0].intValue==123);
  // Another real save invalidates the earlier token; it cannot restore an old secret.
  t=begin("");CHECK(t);web.request("/api/wifi",HTTP_PUT,"{\"ssid\":\"new\",\"password\":\"new-current\"}");
  CHECK(web.status==200&&!commitConfigSave(t,error));t=begin("");CHECK(t);finish(t);CHECK(String(config.password)=="new-current");
  const auto before=disk();
  t=begin(",\"password\":\"abandoned\"");CHECK(t);CHECK(loadConfig(error)); // power loss before commit
  CHECK(disk()==before&&String(config.password)=="new-current"&&!commitConfigSave(t,error));
  storageFault::pendingWriteFailures=1;web.request("/api/wifi",HTTP_PUT,"{\"ssid\":\"failed\"}");
  CHECK(web.status==400&&disk()==before&&String(config.password)=="new-current");storageFault::clearFaults();
  t=begin("");CHECK(t);storageFault::renameFailure=true;
  JsonDocument record;for(uint8_t i=0;i<28;++i){CHECK(encodeChain(c.chains[i],i,record));CHECK(stageConfigChain(t,i,json(record),error));}
  CHECK(encodeRotation(c.encoderRotation,record,true)&&stageConfigRotation(t,json(record),error));
  CHECK(!commitConfigSave(t,error)&&disk()==before);storageFault::clearFaults();CHECK(loadConfig(error)&&String(config.password)=="new-current");
  // Missing credentials in storage stay corrupt; API omission is not a disk migration.
  auto corrupt=before;auto start=corrupt.find("\"password\":");CHECK(start!=std::string::npos);
  auto end=corrupt.find(',',start);CHECK(end!=std::string::npos);corrupt.erase(start,end-start+1);
  *storageFault::files.at("/config.records")=corrupt;CHECK(!loadConfig(error));CHECK(!begin(""));
  *storageFault::files.at("/config.records")=before;CHECK(loadConfig(error));
  std::cout<<"PASS Wi-Fi privacy: real GET routes omit secret, strict disk decode, both update routes preserve/replace/clear/reject, current-value snapshot, token invalidation, reboot/abandon/write/rename failure retention\n";
}
int main(int argc,char** argv){try{
  if(argc==2&&std::string(argv[1])=="--wifi-privacy"){wifiPrivacy();return 0;}
  if(argc==2&&std::string(argv[1]).find("--builtin-")==0){
    const std::string mode=argv[1];String error;probeFake::clear();registerHandlers();
    if(mode=="--builtin-upgrade"){
      for(const char* name:{"default","user","empty"}){
        std::ifstream file(std::string(".pio/storage-fault-tests/builtin-")+name+".records");CHECK(file.good());
        const std::string saved((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        storageFault::files["/config.records"]=std::make_shared<std::string>(saved);
        CHECK(loadConfig(error)&&configOutputsAllowed()&&disk()==saved&&storageFault::renames==0);
        if(std::string(name)=="default"){
          web.request("/api/config",HTTP_GET);CHECK(web.status==200);
          JsonDocument d;CHECK(!deserializeJson(d,web.body));CHECK(d["chains"][0]["actions"][0]["usage"]==36); // Saved Default KEY1: 7.
        }else CHECK(config.chains[0].count==(std::string(name)=="user"?16:0));
      }
      storageFault::files.clear();CHECK(loadConfig(error));
      web.request("/api/config",HTTP_GET);JsonDocument d;CHECK(!deserializeJson(d,web.body));
      CHECK(d["chains"][0]["actions"][0]["usage"]==4); // New Default applies only to Missing.
      CHECK(storageFault::files.empty()&&storageFault::renames==0);
    }else if(mode=="--builtin-reset"){
      storageFault::files["/factory-reset.done"]=std::make_shared<std::string>("FR1:1");
      CHECK(factoryResetBoot()==ResetBoot::Complete&&!factoryResetBlocked());
      CHECK(loadConfig(error)&&configOutputsAllowed()&&config.chains[0].count==1);
      CHECK(storageFault::files.size()==1&&!storageFault::files.count("/config.records"));
      web.request("/api/config",HTTP_GET);CHECK(web.status==200);
      std::ofstream(".pio/storage-fault-tests/builtin-reset.json")<<web.body;
    }else{
      if(mode=="--builtin-chain-oom")scratchFault::size=sizeof(Chain);
      else if(mode=="--builtin-rotation-oom")scratchFault::size=sizeof(EncoderRotationSettings);
      else if(mode=="--builtin-json-oom")jsonFault::arm("all",1);
      else throw std::runtime_error("Unknown builtin mode");
      CHECK(!loadConfig(error)&&!configOutputsAllowed()&&configStorageState()==ConfigStorageState::IoError);
      CHECK(scratchFault::failures>0||jsonFault::failures>0);
      scratchFault::size=0;jsonFault::arm("",0);
      CHECK(!loadConfig(error)); // Missing after failure cannot unlock H3.
      CHECK(!saveWifiConfig("{\"ssid\":\"bad\",\"password\":\"\"}",error));
      for(const auto& c:config.chains)CHECK(c.count==0);
      CHECK(storageFault::files.empty()&&storageFault::renames==0);
    }
    std::cout<<"PASS "<<mode<<": Default only on healthy Missing; no automatic storage writes\n";return 0;
  }
  if(argc==2&&(std::string(argv[1])=="--h1-reboot"||std::string(argv[1])=="--h1-persistent")){
    const bool persistent=std::string(argv[1])=="--h1-persistent";
    std::ifstream file(".pio/storage-fault-tests/h1-active.records");CHECK(file.good());
    const std::string saved((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    storageFault::files["/config.records"]=std::make_shared<std::string>(saved);registerHandlers();String e;
    CHECK(!configCommittedApplyFailed());
    if(persistent){
      storageFault::openFailurePath="/config.records";CHECK(!loadConfig(e)&&!loadConfig(e)&&!configOutputsAllowed());
      storageFault::clearFaults();
      // H3 semantics stay unchanged after a real process restart.
      web.request("/api/config",HTTP_GET);CHECK(web.status==200&&!configCommittedApplyFailed());
      JsonDocument d,n;encodeNetwork(config,n);d["schemaVersion"]=2;d["network"]=n;
      CHECK(!beginConfigSave(json(d),e)&&!saveWifiConfig("{\"ssid\":\"bad\",\"password\":\"\"}",e));CHECK(disk()==saved);
    }
    CHECK(loadConfig(e)&&configOutputsAllowed()&&!configCommittedApplyFailed());
    CHECK(String(config.ssid)=="new-on-disk"&&config.chains[0].count==16&&disk()==saved);
    std::cout<<"PASS SAFETY H1 fresh-process "<<(persistent?"persistent read failure uses H3 lock, then full recovery":"restart recovers committed new Config")<<"\n";return 0;
  }
  if(argc==2&&std::string(argv[1])=="--blank-boot"){
    // Separate process: mounted cache and boot-read latch start at power-on values.
    probeFake::clear();probeFake::mounted=false;probeFake::erased=true;
    CHECK(factoryResetBoot()==ResetBoot::Normal);String error;
    CHECK(loadConfig(error)&&configStorageState()==ConfigStorageState::Missing&&configOutputsAllowed());
    CHECK(storageFault::files.empty()&&config.chains[0].count==1&&storageFault::renames==0);
    registerHandlers();web.request("/api/config",HTTP_GET);CHECK(web.status==200);
    std::ofstream(".pio/storage-fault-tests/builtin-boot.json")<<web.body;
    auto token=stage(config);CHECK(commitConfigSave(token,error));
    std::ofstream(".pio/storage-fault-tests/builtin-default.records")<<disk();
    // A user save replaces the unsaved builtin; subsequent boots keep every byte.
    auto custom=fixture();seed(custom);auto saved=disk();
    web.request("/api/config",HTTP_GET);auto snapshot=web.body;
    CHECK(loadConfig(error)&&disk()==saved);
    web.request("/api/config",HTTP_GET);CHECK(web.status==200&&web.body==snapshot);
    CHECK(config.chains[0].count==16&&config.oscPort==9100);
    std::ofstream(".pio/storage-fault-tests/builtin-user.records")<<disk();
    Config empty;seed(empty);
    std::ofstream(".pio/storage-fault-tests/builtin-empty.records")<<disk();
    std::cout<<"PASS SAFETY H3 fresh process: erased unmountable settings -> normal reset flow -> initial Config mount/format\n";
    return 0;
  }
  registerHandlers();h3();h4();h1();h2();
  std::cout<<"PASS H1/H2/H3/H4 safety regression\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
