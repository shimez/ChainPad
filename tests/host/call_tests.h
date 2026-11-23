#include "chain_call.h"
#include <fstream>
#include <sstream>

void callModelTests() {
  using namespace calls;
  static_assert(sizeof(Action) == 352 && MAX_TOTAL_ACTIONS == 224, "Call must reuse existing storage");
  std::ifstream file("tests/call-model-cases.json");
  std::ostringstream buffer; buffer << file.rdbuf();
  JsonDocument cases;
  assert(file && !deserializeJson(cases, buffer.str().c_str()));
  for (auto example : cases["wire"].as<JsonArray>()) {
    const std::string raw = std::string("{\"input\":0,\"actions\":[") + example[0].as<const char*>() + "]}";
    JsonDocument doc; assert(!deserializeJson(doc, raw.c_str()));
    Chain chain; String error;
    const bool valid = !example[1].isNull();
    assert(decodeModelChain(doc.as<JsonVariantConst>(), 0, chain, error) == valid);
    if (!valid) continue;
    assert(chain.count == 1 && chain.actions[0].protocol == Protocol::CallChain &&
           chain.actions[0].targetInput == example[1].as<unsigned>() && executable(chain));
    assert(encodeChain(chain, 0, doc));
    auto action = doc["actions"][0];
    assert(action.size() == 3 && action["targetInput"].is<uint8_t>() && action["delayMs"] == 0);
    assert(decodeModelChain(doc.as<JsonVariantConst>(), 0, chain, error));
    assert(decodeChain(doc.as<JsonVariantConst>(), 0, chain, error));
  }
  auto appendCall = [](ChainView& chain, uint8_t target) {
    auto& a = chain.actions[chain.count++]; a.protocol = Protocol::CallChain; a.targetInput = target;
  };
  Config cfg;
  auto check = [&](Cycle& cycle) {
    Graph g; for (int i=INPUT_COUNT-1; i>=0; --i) assert(g.add(i,cfg.chains[i]) == Error::None);
    return g.validate(cycle);
  };
  for (auto example : cases["graphs"].as<JsonArray>()) {
    for (auto& c : cfg.chains) c.count = 0;
    for (auto edge : example["edges"].as<JsonArray>()) appendCall(cfg.chains[edge[0].as<unsigned>()],edge[1]);
    Cycle cycle; auto expected = example["cycle"].as<JsonArray>();
    assert(check(cycle) == (expected.size() ? Error::Cycle : Error::None));
    assert(cycle.count == expected.size());
    for (unsigned i=0; i<cycle.count; ++i) {
      auto e = cycle.edges[i]; assert(expected[i][0]==e.input && expected[i][1]==e.index && expected[i][2]==e.target);
    }
  }
  for (auto& c : cfg.chains) c.count = 0;
  for (uint8_t i=0; i<27; ++i) appendCall(cfg.chains[i], i+1);
  Cycle cycle; assert(check(cycle)==Error::None);
  appendCall(cfg.chains[27],0); assert(check(cycle)==Error::Cycle && cycle.count==28);
  Graph g; assert(g.add(1,cfg.chains[1])==Error::None && g.add(1,cfg.chains[1])==Error::Duplicate);
  assert(g.validate(cycle)==Error::Missing);
  for (auto& c : cfg.chains) c.count = 0;
  for (const uint8_t id : {0,2}) {
    const auto depth = id ? 8 : 7;
    std::string raw = "{\"input\":" + std::to_string(id) + ",\"actions\":[";
    for (int k=0; k<depth; ++k) raw += "{\"protocol\":\"loopStart\",\"delayMs\":0,\"count\":2},";
    if (!id) raw += "{\"protocol\":\"callChain\",\"delayMs\":0,\"targetInput\":2},";
    for (int k=0; k<depth; ++k) raw += "{\"protocol\":\"loopEnd\",\"delayMs\":0},";
    raw.back()=']'; raw+='}';
    JsonDocument doc; Chain chain; String error;
    assert(!deserializeJson(doc,raw.c_str()) && decodeModelChain(doc.as<JsonVariantConst>(),id,chain,error));
    cfg.chains[id].assign(chain);
  }
  assert(check(cycle)==Error::None); // No cross-Chain sum of Loop depths.
  Chain over; over.count=9; Graph limits;
  assert(limits.add(26,over)==Error::Slots && limits.add(28,over)==Error::InvalidInput);
  assert(limits.add(0,cfg.chains[0])==Error::None && limits.add(1,over)==Error::Slots);
  over.count=1; over.actions[0].protocol=Protocol::CallChain; over.actions[0].targetInput=28;
  assert(limits.add(2,over)==Error::InvalidCall);
  std::cout << "PASS Call model: shared wire/graph cases, codec roundtrip, release view, runtime gate\n";
}
