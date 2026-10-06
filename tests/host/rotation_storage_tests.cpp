#include "model.h"
#include "LittleFS.h"
#include "save_helper.h"
#include "json_wire.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace chimera;
extern bool rejectConfigAllocation;
extern int rotationAllocationsUntilFailure;
namespace {
String body(JsonDocument& doc) { String s; serializeJson(doc,s); return s; }
uint32_t begin(const Config& c, String& error, bool replace=false) {
  JsonDocument network, request; encodeNetwork(c,network);
  request["schemaVersion"]=2;request["network"]=network;
  if(replace)request["replaceUnsupported"]=true;
  return beginConfigSave(body(request),error);
}
void chains(uint32_t token,const Config& c,String& error) {
  JsonDocument doc;
  for(uint8_t i=0;i<INPUT_COUNT;++i){encodeChain(c.chains[i],i,doc);assert(stageConfigChain(token,i,body(doc),error));}
}
}
void rotationStorageTests() {
  Config c; String error; JsonDocument doc;
  auto& r=c.encoderRotation;r.mode=RotationMode::RotationValue;r.axis={65535,32768,RotationBoundary::Wrap};r.outputCount=16;
  for(unsigned i=0;i<16;++i){
    auto& o=r.outputs[i];
    memset(o.address,'"',192);o.address[0]='/';o.address[192]=0;
    if(i%3==0){o.kind=RotationOutputKind::OscFloat;o.range.floating={std::numeric_limits<float>::max(),std::numeric_limits<float>::denorm_min()};}
    else if(i%3==1){o.range.integer={INT32_MIN,INT32_MAX};}
    else{o.kind=RotationOutputKind::MidiCC;o.transport=Transport::Both;o.range.integer={127,0};}
  }
  // All 224 Actions remain allocated and persisted, including inactive CW/CCW.
  for(uint8_t i=0;i<28;++i){auto& chain=c.chains[i];chain.count=i>=26?8:i%2?0:16;
    for(unsigned k=0;k<chain.count;++k){auto& a=chain.actions[k];a.oscType=OscType::String;
      memset(a.address,'"',192);a.address[0]='/';a.address[192]=0;memset(a.stringValue,1,128);a.stringValue[128]=0;}}
  rejectConfigAllocation=true;
  assert(saveSource(c,error));assert(loadConfig(error));
  rejectConfigAllocation=false;
  assert(config.encoderRotation.outputCount==16 && config.encoderRotation.axis.initialPosition==32768);
  assert(config.encoderRotation.outputs[0].range.floating.start==std::numeric_limits<float>::max());
  assert(config.encoderRotation.outputs[0].range.floating.end==std::numeric_limits<float>::denorm_min());
  const auto original=*fake::files.at("/config.records");
  assert(original.find("\\u0001")!=std::string::npos && original.find('\x01')==std::string::npos);
  encodeChain(c.chains[0],0,doc);String strictJson;serializeSettingsJson(doc,strictJson);
  assert(strictJson.length()==measureSettingsJson(doc) && strictJson.length()<=MAX_RECORD_BYTES);
  assert(original.find("currentPosition")==std::string::npos && original.find("generation")==std::string::npos);
  encodeConfig(c,doc); Config copied=c; copied.chains[0].actions[0].intValue=123;
  copied.encoderRotation.outputs[1].range.integer.start=42;
  assert(c.encoderRotation.outputs[1].range.integer.start==INT32_MIN);
  assert(&copied.chains[0].actions[0]!=&c.chains[0].actions[0]);
  for(double bad : {0.,65536.,1.5,-1.,1e20}){
    encodeRotation(r,doc);doc["rotationValue"]["rangeSteps"]=bad;
    EncoderRotationSettings decoded;assert(!decodeRotation(doc.as<JsonVariantConst>(),decoded,error));
  }
  for(const char* key : {"initialPosition","rangeSteps"}){
    encodeRotation(r,doc);doc["rotationValue"][key]="20";
    EncoderRotationSettings decoded;assert(!decodeRotation(doc.as<JsonVariantConst>(),decoded,error));
  }
  for(const char* key : {"channel","number","start","end"}){
    encodeRotation(r,doc);doc["rotationValue"]["outputs"][2][key]=1.5;
    EncoderRotationSettings decoded;assert(!decodeRotation(doc.as<JsonVariantConst>(),decoded,error));
  }
  for(double bad : {-2147483649.,2147483648.,-0.5}){
    encodeRotation(r,doc);doc["rotationValue"]["outputs"][1]["start"]=bad;
    EncoderRotationSettings decoded;assert(!decodeRotation(doc.as<JsonVariantConst>(),decoded,error));
  }
  r.mode=RotationMode::ActionChain; // Still validate inactive outputs.
  encodeRotation(r,doc);doc["rotationValue"]["outputs"][0]["start"]=1e100;
  EncoderRotationSettings decoded;assert(!decodeRotation(doc.as<JsonVariantConst>(),decoded,error));
  encodeRotation(r,doc);doc["rotationValue"]["outputs"].as<JsonArray>().add(doc["rotationValue"]["outputs"][0]);
  assert(!decodeRotation(doc.as<JsonVariantConst>(),decoded,error));
  for(const char* bad : {"{}","{\"schemaVersion\":1}","{\"schemaVersion\":2.5}","{\"schemaVersion\":\"2\"}"}){
    assert(!beginConfigSave(bad,error));assert(*fake::files.at("/config.records")==original);
  }
  auto token=begin(c,error);assert(token);assert(!commitConfigSave(token,error));
  chains(token,c,error);assert(!commitConfigSave(token,error));
  assert(*fake::files.at("/config.records")==original && config.encoderRotation.mode==RotationMode::RotationValue);
  encodeRotation(r,doc,true);assert(stageConfigRotation(token,body(doc),error));
  const auto pending=fake::files.at("/pending.records")->size();
  assert(commitConfigSave(token,error));assert(loadConfig(error));
  assert(config.encoderRotation.mode==RotationMode::ActionChain);
  assert(config.chains[26].count==8 && config.chains[27].count==8);
  const auto savedRotation=config.encoderRotation;
  rejectConfigAllocation=true;
  assert(saveWifiConfig("{\"ssid\":\"phase-b\",\"password\":\"test-only\"}",error));
  assert(loadConfig(error)); rejectConfigAllocation=false;
  encodeRotation(savedRotation,doc);auto expected=body(doc);encodeRotation(config.encoderRotation,doc);assert(body(doc)==expected);
  const auto v2=*fake::files.at("/config.records");
  token=begin(c,error);chains(token,c,error);encodeRotation(r,doc,true);
  rotationAllocationsUntilFailure=0;
  assert(!stageConfigRotation(token,body(doc),error));
  rotationAllocationsUntilFailure=-1;
  assert(*fake::files.at("/config.records")==v2 && configOutputsAllowed());
  token=begin(c,error);chains(token,c,error);encodeRotation(r,doc,true);
  assert(stageConfigRotation(token,body(doc),error));
  rotationAllocationsUntilFailure=1; // Validation succeeds; post-rename apply fails.
  assert(!commitConfigSave(token,error) && !configOutputsAllowed() && configStorageState()==ConfigStorageState::IoError);
  rotationAllocationsUntilFailure=-1;
  assert(loadConfig(error) && configOutputsAllowed());
  // Real legacy header and 28 records; no migration or file modification.
  auto legacy=v2;auto pos=legacy.find("\"storageVersion\":2");assert(pos!=std::string::npos);
  legacy.replace(pos,18,"\"storageVersion\":1");legacy.erase(legacy.find_last_of('\n',legacy.size()-2)+1);
  *fake::files.at("/config.records")=legacy;
  assert(!loadConfig(error) && configStorageState()==ConfigStorageState::UnsupportedVersion && !configOutputsAllowed());
  assert(!begin(c,error));assert(!saveWifiConfig("{\"ssid\":\"x\",\"password\":\"\"}",error));
  assert(*fake::files.at("/config.records")==legacy);
  token=begin(c,error,true);assert(token);chains(token,c,error);assert(!commitConfigSave(token,error));
  assert(*fake::files.at("/config.records")==legacy && !configOutputsAllowed());
  encodeRotation(r,doc,true);assert(stageConfigRotation(token,body(doc),error));assert(commitConfigSave(token,error));
  assert(loadConfig(error)&&configStorageState()==ConfigStorageState::Ready&&configOutputsAllowed());
  *fake::files.at("/config.records")="not json\n";
  assert(!loadConfig(error)&&configStorageState()==ConfigStorageState::Corrupt);
  fake::files.erase("/config.records");
  assert(loadConfig(error)&&configStorageState()==ConfigStorageState::Missing);
  * (fake::files["/config.records"]=std::make_shared<std::string>())=v2;
  assert(loadConfig(error));
  std::cout<<"PASS v2 Rotation storage / max224+16 / pending="<<pending<<" active="<<v2.size()<<" bytes / legacy protection / explicit replacement / Wi-Fi preservation / strict numbers\n";
}
