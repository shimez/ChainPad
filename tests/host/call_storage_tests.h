namespace chimera {
bool commitCallModelForTest(uint32_t token, String& error);
bool loadCallModelForTest(String& error);
}

void callStorageTests() {
  Config original, source, decoded;
  String error;
  original.oscPort = 9876;
  assert(saveSource(original,error));
  const auto baseline = *fake::files.at("/config.records");
  auto& call = source.chains[1].actions[0];
  source.chains[1].count = 1; call.protocol = Protocol::CallChain; call.targetInput = 27;
  auto stage = [&]() {
    JsonDocument doc, network; String body;
    assert(encodeNetwork(source,network)); doc["schemaVersion"]=2; doc["network"]=network;
    serializeJson(doc,body); auto t=beginConfigSave(body,error); assert(t);
    for (uint8_t i=0; i<INPUT_COUNT; ++i) {
      assert(encodeChain(source.chains[i],i,doc)); body=""; serializeJson(doc,body);
      assert(stageConfigChain(t,i,body,error)); // input 1 refers to input 27, not staged yet
    }
    assert(encodeRotation(source.encoderRotation,doc,true)); body=""; serializeJson(doc,body);
    assert(stageConfigRotation(t,body,error)); return t;
  };
  auto unchanged = [&]() {
    assert(*fake::files.at("/config.records")==baseline && encoded(config)==encoded(original));
    assert(configOutputsAllowed());
  };
  auto bootRejected = [&](const std::string& bytes, ConfigStorageState state, const char* detail) {
    *fake::files.at("/config.records") = bytes;
    assert(!loadConfig(error) && configStorageState()==state && !configOutputsAllowed());
    assert(std::string(error.c_str()).find(detail)!=std::string::npos);
    for (const auto& c : config.chains) assert(c.count==0);
    assert(!saveSource(original,error) && *fake::files.at("/config.records")==bytes);
    *fake::files.at("/config.records") = baseline;
    assert(loadConfig(error)); unchanged();
  };
  auto t=stage();
  assert(!commitConfigSave(t,error) && error=="Call Chain is unsupported by this firmware"); unchanged();
  const auto forward=*fake::files.at("/pending.records");
  bootRejected(forward,ConfigStorageState::UnsupportedFeature,"unsupported");
  assert(commitCallModelForTest(stage(),error) && *fake::files.at("/config.records")==forward);
  assert(loadCallModelForTest(error) && encoded(config)==encoded(source)); // no Engine trigger
  assert(saveSource(original,error)); unchanged();

  source.chains[27].count=1;
  source.chains[27].actions[0]=call; source.chains[27].actions[0].targetInput=1;
  const char* cycle="Call cycle: key1.release#1 -> encoder.ccw#1 -> key1.release";
  JsonDocument full; assert(encodeConfig(source,full));
  assert(!decodeConfig(full.as<JsonVariantConst>(),decoded,error) && error==cycle);
  assert(!commitConfigSave(stage(),error) && error==cycle); unchanged();
  assert(!commitCallModelForTest(stage(),error) && error==cycle); unchanged();
  bootRejected(*fake::files.at("/pending.records"),ConfigStorageState::Corrupt,cycle);

  auto invalid=forward;
  auto pos=invalid.find("\"targetInput\":27"); assert(pos!=std::string::npos);
  invalid.replace(pos,16,"\"targetInput\":28");
  bootRejected(invalid,ConfigStorageState::Corrupt,"Invalid Call targetInput at key1.release#1");
  t=beginConfigSave(R"({"schemaVersion":2,"network":{"ssid":"","password":"","oscHost":"127.0.0.1","oscPort":9000}})",error);
  assert(t && stageConfigChain(t,0,R"({"input":0,"actions":[]})",error));
  assert(!stageConfigChain(t,1,R"({"input":1,"actions":[{"protocol":"callChain","delayMs":0,"targetInput":28}]})",error));
  assert(error=="Invalid Call targetInput at key1.release#1"); unchanged();
  t=stage(); *fake::files.at("/pending.records")=invalid;
  assert(!commitConfigSave(t,error) && error=="Invalid Call targetInput at key1.release#1"); unchanged();
  full["chains"][1]["actions"][0]["targetInput"]="27";
  assert(!decodeConfig(full.as<JsonVariantConst>(),decoded,error) && error=="Invalid Call targetInput at key1.release#1");
  std::cout<<"PASS Call storage: forward stage, model-only roundtrip, cycles before admission, invalid target, preserved active, locked boot\n";
}
