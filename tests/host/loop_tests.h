#include "chain_loop.h"

void loopFoundationTests() {
  static_assert(sizeof(Action) == 352, "Loop count must not enlarge the 224-slot Action pool");
  static_assert(sizeof(loops::Layout) == MAX_ACTIONS + 2, "Loop layout must remain bounded");
  static_assert(loops::MAX_DEPTH * 2 == MAX_ACTIONS, "Depth follows the Action budget");
  static_assert(unsigned(Protocol::Obs) == 5, "Existing protocol values must not change");
  Action header; header.protocol = Protocol::LoopStart; header.loopCount = 3;
  Action footer; footer.protocol = Protocol::LoopEnd; footer.loopCount = 0;
  Action ordinary; ordinary.protocol = Protocol::Wait; ordinary.delayMs = 100;

  auto parse = [](const char* raw, Action& action) {
    JsonDocument doc;
    assert(!deserializeJson(doc, raw));
    return loops::parse(doc.as<JsonVariantConst>(), action);
  };
  for (const unsigned count : {1u, 3u, 999u}) {
    auto source = header; source.loopCount = count;
    JsonDocument doc; auto object = doc.to<JsonObject>();
    object["oldUiId"] = 42; // Canonical encoder drops unrelated metadata.
    assert(loops::encode(source, object));
    assert(object.size() == 3 && !object.containsKey("oldUiId"));
    Action decoded;
    assert(loops::parse(doc.as<JsonVariantConst>(), decoded) == loops::ParseResult::Valid);
    assert(decoded.protocol == Protocol::LoopStart && decoded.loopCount == count && decoded.delayMs == 0);
    assert(!pairedAction(decoded));
    JsonDocument again; assert(loops::encode(decoded, again.to<JsonObject>()));
    String first, second; serializeJson(doc, first); serializeJson(again, second);
    assert(first == second);
  }
  {
    JsonDocument doc; assert(loops::encode(footer, doc.to<JsonObject>()));
    assert(doc.size() == 2 && !doc["count"].is<unsigned>());
    Action decoded;
    assert(loops::parse(doc.as<JsonVariantConst>(), decoded) == loops::ParseResult::Valid);
    assert(decoded.protocol == Protocol::LoopEnd && decoded.loopCount == 0);
  }
  for (const char* raw : {
      R"({"protocol":"loopStart","delayMs":0})",
      R"({"protocol":"loopStart","delayMs":0,"count":0})",
      R"({"protocol":"loopStart","delayMs":0,"count":-1})",
      R"({"protocol":"loopStart","delayMs":0,"count":1000})",
      R"({"protocol":"loopStart","delayMs":0,"count":65536})",
      R"({"protocol":"loopStart","delayMs":0,"count":18446744073709551615})",
      R"({"protocol":"loopStart","delayMs":0,"count":1.5})",
      R"({"protocol":"loopStart","delayMs":0,"count":3.0})",
      R"({"protocol":"loopStart","delayMs":0,"count":true})",
      R"({"protocol":"loopStart","delayMs":0,"count":"3"})",
      R"({"protocol":"loopStart","delayMs":0,"count":null})",
      R"({"protocol":"loopStart","count":3})",
      R"({"protocol":"loopStart","delayMs":false,"count":3})",
      R"({"protocol":"loopStart","delayMs":"0","count":3})",
      R"({"protocol":"loopStart","delayMs":0.0,"count":3})",
      R"({"protocol":"loopStart","delayMs":1,"count":3})",
      R"({"protocol":"loopStart","delayMs":0,"count":3,"id":"x"})",
      R"({"protocol":"loopStart","delayMs":0,"count":3,"collapsed":true})",
      R"({"protocol":"loopStart","delayMs":0,"count":3,"holdMs":0})",
      R"({"protocol":"loopStart\u0000suffix","delayMs":0,"count":3})",
      R"({"protocol":"loopEnd"})",
      R"({"protocol":"loopEnd","delayMs":null})",
      R"({"protocol":"loopEnd","delayMs":1})",
      R"({"protocol":"loopEnd","delayMs":0,"count":3})",
      R"({"protocol":"loopEnd","delayMs":0,"target":0})",
      R"({"protocol":"loopEnd\u0000suffix","delayMs":0})"}) {
    Action decoded = ordinary;
    assert(parse(raw, decoded) == loops::ParseResult::Invalid);
    assert(decoded.protocol == Protocol::Wait && decoded.delayMs == 100);
  }
  for (const char* raw : {R"({"protocol":"wait","delayMs":100})", R"({"protocol":"osc"})",
                          R"({"protocol":"unknown"})", R"({"protocol":null})", "null", "[]"}) {
    Action decoded = ordinary;
    assert(parse(raw, decoded) == loops::ParseResult::NotLoop);
    assert(decoded.protocol == ordinary.protocol && decoded.delayMs == ordinary.delayMs);
  }
  for (const unsigned count : {0u, 1000u, 0xffffffffu}) {
    auto invalid = header; invalid.loopCount = count;
    JsonDocument doc; doc["keep"] = 17;
    assert(!loops::encode(invalid, doc.as<JsonObject>()));
    assert(doc.size() == 1 && doc["keep"] == 17);
  }
  {
    auto invalid = header; invalid.delayMs = 1;
    JsonDocument doc; doc["keep"] = 17;
    assert(!loops::encode(invalid, doc.as<JsonObject>()));
    assert(!loops::encode(ordinary, doc.as<JsonObject>()));
    assert(doc["keep"] == 17);
  }

  auto makeChain = [](std::initializer_list<Action> actions) {
    Chain chain; for (const auto& action : actions) chain.actions[chain.count++] = action;
    return chain;
  };
  String error; loops::Layout layout;
  auto chain = makeChain({ordinary, header, ordinary, header, ordinary, footer, footer, header, footer});
  assert(loops::analyze(chain, layout, error) && error.isEmpty());
  assert(layout.count == 3 && layout.maxDepth == 2);
  assert(layout.partner[1] == 6 && layout.partner[6] == 1);
  assert(layout.partner[3] == 5 && layout.partner[5] == 3);
  assert(layout.partner[7] == 8 && layout.partner[8] == 7);
  for (auto index : {0, 2, 4, 9, 15}) assert(layout.partner[index] == loops::NO_PARTNER);
  // Empty and sequential loops are valid; no artificial semantic depth limit.
  for (uint8_t limit : {uint8_t(8), MAX_ACTIONS}) {
    Chain nested; nested.count = limit;
    for (uint8_t i = 0; i < limit / 2; ++i) nested.actions[i] = header;
    for (uint8_t i = limit / 2; i < limit; ++i) nested.actions[i] = footer;
    assert(loops::analyze(nested, layout, error, limit));
    assert(layout.maxDepth == limit / 2);
    for (uint8_t i = 0; i < limit; ++i) assert(layout.partner[i] == limit - i - 1);
  }
  chain = makeChain({ordinary});
  assert(loops::analyze(chain, layout, error) && layout.count == 0 && layout.maxDepth == 0);
  chain.count = 0; assert(loops::analyze(chain, layout, error));
  for (const auto& invalid : {
      makeChain({footer}), makeChain({header}), makeChain({footer, header}),
      makeChain({header, ordinary}), makeChain({header, footer, footer}),
      makeChain({header, header, footer})}) {
    layout.partner[0] = 3; layout.count = 5;
    assert(!loops::analyze(invalid, layout, error) && !error.isEmpty());
    assert(layout.count == 0 && layout.maxDepth == 0);
    for (auto partner : layout.partner) assert(partner == loops::NO_PARTNER);
  }
  chain = makeChain({header, footer});
  chain.actions[0].loopCount = 0; assert(!loops::analyze(chain, layout, error));
  chain.actions[0].loopCount = 1000; assert(!loops::analyze(chain, layout, error));
  chain.actions[0] = header; chain.actions[1].delayMs = 1; assert(!loops::analyze(chain, layout, error));
  // Counts are checked before reading the Action array.
  chain.count = MAX_ACTIONS + 1; assert(!loops::analyze(chain, layout, error));
  chain.count = 9; assert(!loops::analyze(chain, layout, error, eventActionLimit(26)));
  chain.count = MAX_ACTIONS;
  for (auto& action : chain.actions) action = header;
  assert(!loops::analyze(chain, layout, error)); // Cannot close 16 headers within 16 slots.
  {
    Config source;
    const auto nested = makeChain({header, ordinary, header, ordinary, footer, footer});
    source.chains[0].assign(nested); source.chains[1].assign(nested);
    loops::Layout press, release;
    assert(loops::analyze(source.chains[0], press, error));
    assert(loops::analyze(source.chains[1], release, error));
    for (unsigned i = 0; i < MAX_ACTIONS; ++i) assert(press.partner[i] == release.partner[i]);
    assert(source.chains[1].actions.step == -1); // Logical, not physical, indexing.
  }
  // Phase boundary: do not admit a Loop into the legacy scheduler/save path.
  // Replace this assertion with end-to-end acceptance tests in Engine Phase 2.
  {
    JsonDocument doc;
    assert(!deserializeJson(doc, R"({"input":0,"actions":[{"protocol":"loopStart","delayMs":0,"count":3},{"protocol":"loopEnd","delayMs":0}]})"));
    Chain decoded;
    assert(!decodeChain(doc.as<JsonVariantConst>(), 0, decoded, error));
    assert(dispatch(header, 0) == SendResult::Failed);
    assert(dispatch(footer, 0) == SendResult::Failed);
  }
  std::cout << "PASS Loop foundation: strict codec / count boundaries / canonical round-trip / nested pairing / limits / reverse Release view / legacy admission guard\n";
}
