#pragma once
#include "obs_actions.h"
// Shared by full host and each BLE-only target: real codec and LittleFS path.
void obsActionTests() {
  Config source, decoded; String error;
  source.chains[0].count = 7;
  for (unsigned i=0;i<7;++i) {
    auto& a=source.chains[0].actions[i]; a.protocol=Protocol::Obs;
    a.obsOperation=obs::Operation(i); a.boolValue=i==5;
    strcpy(a.obsTarget,"日本語 🎙 target");
  }
  JsonDocument doc; assert(encodeConfig(source,doc));
  assert(decodeConfig(doc.as<JsonVariantConst>(),decoded,error));
  assert(saveSource(decoded,error));
  config.chains[0].count=0; assert(loadConfig(error));
  JsonDocument restored; assert(encodeConfig(config,restored));
  String before,after; serializeJson(doc,before); serializeJson(restored,after);
  assert(before==after);
  for(unsigned i=0;i<7;++i) {
    assert(config.chains[0].actions[i].obsOperation==obs::Operation(i));
    assert(dispatch(config.chains[0].actions[i],0)==SendResult::Unavailable);
    assert(!strcmp(obsActionStatus().admission,"unsupported"));
  }
  auto a=doc["chains"][0]["actions"][0];
  std::string unicode; for(unsigned i=0;i<64;++i)unicode+="名";
  for(const auto& name:{std::string(192,'x'),unicode}) {
    a["sceneName"]=name; assert(decodeConfig(doc.as<JsonVariantConst>(),decoded,error));
  }
  for(const auto& name:{std::string(193,'x'),unicode+"x",std::string(),std::string("bad\n"),std::string("\xc0\xaf"),std::string("a\0b",3)}) {
    a["sceneName"]=name; assert(!decodeConfig(doc.as<JsonVariantConst>(),decoded,error));
  }
  a["sceneName"]="正常";
  a["password"]="forbidden"; assert(!decodeConfig(doc.as<JsonVariantConst>(),decoded,error)); a.remove("password");
  a["operation"]="unknown"; assert(!decodeConfig(doc.as<JsonVariantConst>(),decoded,error)); a["operation"]="setCurrentProgramScene";
  a["delayMs"]=1; assert(!decodeConfig(doc.as<JsonVariantConst>(),decoded,error)); a["delayMs"]=0;
  doc["chains"][0]["actions"][5]["inputMuted"]=1; assert(!decodeConfig(doc.as<JsonVariantConst>(),decoded,error));
  doc["chains"][0]["actions"][5]["inputMuted"]=false; assert(decodeConfig(doc.as<JsonVariantConst>(),decoded,error));
  a["protocol"]="unknown"; assert(!decodeConfig(doc.as<JsonVariantConst>(),decoded,error));
  engine.cancelAll(); config.chains[0].count=3;
  Action hid; hid.protocol=Protocol::Keyboard; hid.transport=Transport::Ble;
  Action obsAction; obsAction.protocol=Protocol::Obs;
  config.chains[0].actions[0]=hid; config.chains[0].actions[1]=obsAction; config.chains[0].actions[2]=Action{};
  const auto accepted=engine.stats.accepted, skipped=engine.stats.skipped;
  assert(engine.trigger({0,0})); assert(engine.activeCount()==0);
  assert(engine.stats.accepted==accepted+2 && engine.stats.skipped==skipped+1 && engine.stats.lastAction==2);
  backendsPanic(); engine.stats={};
  std::cout<<"PASS OBS: seven-operation codec/storage roundtrip, strict parameters, unsupported dispatch and HID/OBS/OSC continuation\n";
}
