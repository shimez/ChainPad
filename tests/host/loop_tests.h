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
  // Production codec accepts validated control markers; backends never send them.
  {
    JsonDocument doc;
    assert(!deserializeJson(doc, R"({"input":0,"actions":[{"protocol":"loopStart","delayMs":0,"count":3},{"protocol":"loopEnd","delayMs":0}]})"));
    Chain decoded;
    assert(decodeChain(doc.as<JsonVariantConst>(), 0, decoded, error));
    assert(dispatch(header, 0) == SendResult::Failed);
    assert(dispatch(footer, 0) == SendResult::Failed);
  }
  std::cout << "PASS Loop foundation: strict codec / count boundaries / canonical round-trip / nested pairing / limits / reverse Release view / production admission\n";
}

void loopEngineTests() {
  Action start; start.protocol = Protocol::LoopStart; start.loopCount = 3;
  Action end; end.protocol = Protocol::LoopEnd;
  Action osc; osc.protocol = Protocol::Osc;
  Action wait; wait.protocol = Protocol::Wait; wait.delayMs = 10;
  Action stop; stop.protocol = Protocol::StopOtherChains;
  auto fresh = [&] {
    reset(); config = Config{}; hostMillis = 0; fake::wifi = true;
    fake::packets.clear();
  };
  auto chain = [&](unsigned input, std::initializer_list<Action> actions) {
    auto& c = config.chains[input]; c.count = 0;
    for (const auto& a : actions) c.actions[c.count++] = a;
  };
  auto cycle = [](Engine& e, uint32_t now) {
    hostMillis = now; e.beginCycle(); backendsTick(now); e.tick(now);
  };
  // Nested counters are re-created each outer iteration; logical Release order.
  for (unsigned input : {0u, 1u, 26u, 27u}) {
    fresh(); Engine e;
    chain(input, {start, osc, start, osc, end, end});
    assert(e.trigger({uint8_t(input), 0}));
    for (unsigned i = 0; i < 100 && e.activeCount(); ++i) cycle(e, i);
    assert(!e.activeCount() && fake::packets.size() == 12);
    assert(e.stats.accepted == 12 && e.stats.skipped == 0);
  }
  // Last-iteration Wait, zero Wait, independent retriggers and millis wrap.
  for (uint32_t origin : {0u, 0xfffffff8u}) {
    fresh(); Engine e; hostMillis = origin;
    chain(0, {start, osc, wait, end, osc});
    assert(e.trigger({0, 0}));
    assert(fake::packets.size() == 1);
    for (unsigned step = 1; step <= 3; ++step) {
      cycle(e, origin + step * 10 - 1);
      assert(fake::packets.size() == step);
      cycle(e, origin + step * 10);
      assert(fake::packets.size() == step + 1);
    }
    assert(e.activeCount() == 0);
  }
  fresh();
  {
    Engine e; Action zero = wait; zero.delayMs = 0;
    chain(0, {start, zero, osc, end});
    assert(e.trigger({0,0}) && fake::packets.size() == 1);
    for (unsigned i = 1; i <= 8; ++i) cycle(e, i);
    assert(!e.activeCount() && fake::packets.size() == 3);
  }
  // Full queue rejection is atomic, including an output before the Header.
  // Stop is an exception only in the non-yielding prefix before that Header.
  for (unsigned variant = 0; variant < 5; ++variant) {
    fresh(); Engine e; chain(0, {wait});
    for (unsigned i = 0; i < Engine::MAX_RUNNING; ++i) assert(e.trigger({0,0}));
    if (variant == 0) chain(2, {osc, start, osc, end});
    if (variant == 1) chain(2, {start, stop, osc, end});
    if (variant == 2) chain(2, {wait, stop, start, osc, end});
    if (variant == 3) { auto zero = wait; zero.delayMs = 0; chain(2, {zero, stop, start, osc, end}); }
    if (variant == 4) chain(2, {osc, stop, start, osc, end});
    assert(e.trigger({2,0}) == (variant == 4));
    assert(fake::packets.size() == (variant == 4 ? 2u : 0u));
    assert(e.activeCount() == (variant == 4 ? 1 : 32));
    assert(e.stats.cancelledChains == (variant == 4 ? 32u : 0u));
    if (variant == 4) {
      for (unsigned i = 1; i <= 8; ++i) cycle(e, i);
      assert(!e.activeCount() && fake::packets.size() == 4);
    }
  }
  // Empty/maximal nesting cannot spin. 32 invocations all make progress.
  fresh();
  {
    Engine e; auto many = start; many.loopCount = 999;
    chain(0, {many, osc, end});
    for (unsigned i = 0; i < 32; ++i) assert(e.trigger({0,0}));
    assert(e.activeCount() == 32 && fake::packets.size() <= Engine::LOOP_CYCLE_BUDGET);
    const auto before = e.stats.accepted;
    e.tick(0); // No budget reset through tick or new trigger.
    assert(e.stats.accepted == before);
    for (unsigned i = 0; i < 33000 && e.activeCount(); ++i) cycle(e, i * Engine::LOOP_OSC_INTERVAL_MS);
    assert(!e.activeCount() && fake::packets.size() == 32u * 999);
    chain(0, {many, many, many, many, many, many, many, many,
              end, end, end, end, end, end, end, end});
    assert(e.trigger({0,0}));
    for (unsigned i = 0; i < 10; ++i) cycle(e, i);
    assert(e.activeCount() == 1);
    e.cancelAll(); assert(!e.activeCount());
    cycle(e, 100); assert(!e.activeCount());
  }
  // Repeated Stop cannot renew either budget; new other jobs are cancelled on
  // the next iteration while self keeps its remaining count.
  fresh();
  {
    Engine e; auto many = start; many.loopCount = 999;
    chain(0, {many, stop, osc, end}); chain(2, {wait, osc});
    assert(e.trigger({0,0}) && e.activeCount() == 1);
    assert(e.stats.accepted <= Engine::LOOP_SLICE_BUDGET);
    for (unsigned i = 0; i < 10; ++i) {
      assert(e.trigger({2,0}));
      cycle(e, (i + 1) * Engine::LOOP_OSC_INTERVAL_MS);
      assert(e.activeCount() == 1);
    }
    assert(e.stats.cancelledChains == 10);
    for (unsigned i = 0; i < 1000 && e.activeCount(); ++i) cycle(e, (i + 11) * Engine::LOOP_OSC_INTERVAL_MS);
    assert(!e.activeCount() && fake::packets.size() == 999);
  }
  // Exhaust budget, then fill all slots: prefix Stop still frees a slot without
  // executing the new Loop until the next cycle.
  fresh();
  {
    Engine e; auto many = start; many.loopCount = 999;
    chain(0, {many, end}); chain(2, {osc, stop, many, osc, end});
    for (unsigned i = 0; i < 32; ++i) assert(e.trigger({0,0}));
    assert(e.trigger({2,0}));
    assert(e.activeCount() == 1 && e.stats.cancelledChains == 32);
    assert(fake::packets.size() == 1);
    cycle(e, 1); assert(fake::packets.size() > 1);
  }
  // Backend unavailable is skipped per iteration, with no retries or abort.
  fresh();
  {
    Engine e; fake::wifi = false; chain(0, {start, osc, end});
    assert(e.trigger({0,0}));
    for (unsigned i = 1; i <= 8; ++i) cycle(e, i);
    assert(!e.activeCount() && e.stats.skipped == 3 && fake::packets.empty());
  }
  // Exhausted Loop budget must not delay or reject an existing straight Chain,
  // including the full-queue exception for synchronous Actions.
  fresh();
  {
    Engine e; auto many = start; many.loopCount = 999;
    chain(0, {many, end}); chain(2, {osc, stop, osc});
    chain(4, {osc, osc});
    for (unsigned i = 0; i < 32; ++i) assert(e.trigger({0,0}));
    assert(e.trigger({4,0}) && fake::packets.size() == 2 && e.activeCount() == 32);
    assert(e.trigger({2,0}) && fake::packets.size() == 4 && !e.activeCount());
    assert(e.stats.cancelledChains == 32);
  }
  // Shared OSC pacing: later jobs cannot overtake waiting Actions.
  fresh();
  {
    Engine e; auto a = osc, b = osc, c = osc;
    strcpy(a.address, "/a"); strcpy(b.address, "/b"); strcpy(c.address, "/c");
    chain(0, {start, a, end}); chain(2, {start, b, end}); chain(4, {start, c, end});
    assert(e.trigger({0,0}) && e.trigger({2,0}) && e.trigger({4,0}));
    assert(e.stats.accepted == 1 && fake::packets.size() == 1);
    for (unsigned i = 0; i < 4; ++i) cycle(e, i);
    assert(e.stats.accepted == 1 && e.stats.skipped == 0);
    cycle(e, 4); cycle(e, 8); cycle(e, 12);
    assert(fake::packets.size() == 4);
    assert(fake::packets[1][1] == 'a' && fake::packets[2][1] == 'b' && fake::packets[3][1] == 'c');
    // Ordinary single-shot sends immediately even while the Loop rate is capped.
    chain(6, {osc}); assert(e.trigger({6,12}) && fake::packets.size() == 5);
    chain(8, {stop}); assert(e.trigger({8,12}) && !e.activeCount());
    assert(e.trigger({2,12}) && e.activeCount() == 1);
    cycle(e, 16); assert(fake::packets.back()[1] == 'b');
    e.cancelAll(); cycle(e, 100); assert(!e.activeCount());
  }
  fresh();
  {
    Engine e; hostMillis = 0xfffffffeu; fake::udpWritable = false;
    chain(0, {start, osc, end}); assert(e.trigger({0,0}));
    assert(e.stats.skipped == 1 && e.stats.accepted == 0);
    cycle(e, 1); assert(e.stats.skipped == 1); // Three ms across rollover.
    fake::udpWritable = true;
    cycle(e, 2); assert(e.stats.accepted == 1 && e.stats.skipped == 1);
    cycle(e, 6); assert(!e.activeCount() && e.stats.accepted == 2 && fake::packets.size() == 2);
  }
  // Loop Stop preserves queued/held output. Panic explicitly discards it and
  // cancels the stack, so a subsequent trigger starts from the first iteration.
  fresh();
  {
    Engine e; Action key; key.protocol = Protocol::Keyboard;
    key.transport = Transport::Usb; key.keyMessage = KeyMessage::Down; key.usage = 4;
    chain(0, {start, stop, key, wait, end});
    assert(e.trigger({0,0}) && e.activeCount() == 1);
    assert(!fake::hid.empty() && fake::hid.back().keys[0] == 4);
    e.cancelAll(); backendsPanic(); settle();
    assert(fake::hid.back().keys[0] == 0);
    cycle(e, 100); assert(!e.activeCount());
    assert(e.trigger({0,100}));
    for (unsigned i = 0; i < 3; ++i) cycle(e, 110 + i * 10);
    assert(!e.activeCount());
  }
  // Paired output must complete on every iteration before the next Action.
  fresh();
  {
    Engine e; Action pair; pair.protocol = Protocol::Midi; pair.transport = Transport::Usb;
    pair.message = MidiMessage::NoteOnOff; pair.delayMs = 5;
    chain(0, {start, pair, osc, end});
    assert(e.trigger({0,0}) && fake::packets.empty());
    for (unsigned i = 0; i < 100 && e.activeCount(); ++i) cycle(e, i);
    assert(!e.activeCount() && fake::packets.size() == 3 && fake::midi.size() == 6);
  }
  // Every Event and LittleFS round-trip; malformed pairs fail before save.
  fresh();
  {
    Config source, restored; String error; JsonDocument doc;
    for (unsigned i = 0; i < INPUT_COUNT; ++i) {
      source.chains[i].count = 4;
      source.chains[i].actions[0] = start; source.chains[i].actions[1] = wait;
      source.chains[i].actions[2] = osc; source.chains[i].actions[3] = end;
    }
    assert(encodeConfig(source, doc) && decodeConfig(doc.as<JsonVariantConst>(), restored, error));
    assert(saveSource(restored, error)); config = Config{}; assert(loadConfig(error));
    for (unsigned i = 0; i < INPUT_COUNT; ++i)
      assert(config.chains[i].actions[0].loopCount == 3 && config.chains[i].actions[3].protocol == Protocol::LoopEnd);
    doc["chains"][0]["actions"][3]["protocol"] = "wait";
    assert(!decodeConfig(doc.as<JsonVariantConst>(), restored, error));
    JsonDocument network, request; encodeNetwork(source, network);
    request["schemaVersion"] = 2; request["network"] = network;
    String raw; serializeJson(request, raw);
    const auto token = beginConfigSave(raw, error); assert(token);
    raw = ""; serializeJson(doc["chains"][0], raw);
    assert(!stageConfigChain(token, 0, raw, error));
    assert(!commitConfigSave(token, error));
    assert(config.chains[0].actions[3].protocol == Protocol::LoopEnd);
  }
  fresh();
  std::cout << "PASS Loop Engine: nested counters / reverse Release / Wait wrap / full admission / shared budgets / fairness / repeated Stop / cancellation / paired output / unavailable / storage; Engine=" << sizeof(Engine) << "\n";
}
