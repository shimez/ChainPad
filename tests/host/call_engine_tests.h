void callFrameTests() {
  InvocationFrames f{};
  uint8_t id=0, pc=0;
  assert(!f.restore(id,pc) && !f.endLoop(pc));
  assert(!f.enterLoop(0,1) && !f.enterLoop(1000,1) && !f.suspend(28,0) && !f.suspend(0,17));
  assert(f.enterLoop(999,15));
  for (unsigned n=1; n<999; ++n) { pc=0; assert(f.endLoop(pc) && pc==15); }
  pc=16; assert(f.endLoop(pc) && pc==16 && !f.top);

  // Physical capacity test, independent of the smaller reachable layouts
  // imposed by actual Action budgets. Reject the 113th push without damage.
  for (uint8_t level=0;level<14;++level) {
    for (uint8_t k=0;k<8;++k) assert(f.enterLoop(1,k+1));
    assert(!f.enterLoop(1,9));
    assert(f.suspend(level,16));
    assert(!f.endLoop(pc)); // Child cannot pop a suspended parent.
  }
  assert(f.top==112 && !f.enterLoop(1,1));
  for (int level=13;level>=0;--level) {
    assert(f.restore(id,pc) && id==level && pc==16 && f.local==8);
    assert(!f.restore(id,pc)); // A child may not return with open Loops.
    for (int k=0;k<8;++k) assert(f.endLoop(pc));
  }
  assert(!f.depth && !f.local && !f.top);
  for (int n=0;n<27;++n) assert(f.suspend(27,16));
  assert(!f.suspend(27,16));
  for (int n=0;n<27;++n) assert(f.restore(id,pc) && id==27 && pc==16);
  assert(!f.restore(id,pc));
}

void callEngineTests() {
  callFrameTests();
  auto fresh=[] { reset(); config=Config{}; hostMillis=0; };
  auto call=[](uint8_t id){ Action a; a.protocol=Protocol::CallChain; a.targetInput=id; return a; };
  auto mark=[](uint8_t id){ Action a; a.protocol=Protocol::Midi; a.transport=Transport::Ble;
    a.message=MidiMessage::CC; a.number=id; return a; };
  auto marks=[] { std::vector<int> v; for (auto m:fake::midi) v.push_back(m.a); return v; };
  auto add=[](uint8_t id,Action a){ auto& c=config.chains[id]; c.actions[c.count++]=a; };
  auto drain=[](Engine& e){ for(int n=0;n<1000 && e.activeCount();++n){
    ++hostMillis; e.beginCycle(); e.tick(hostMillis);
  } assert(!e.activeCount()); };
  Action start; start.protocol=Protocol::LoopStart; start.loopCount=2;
  Action end; end.protocol=Protocol::LoopEnd;
  Action wait; wait.protocol=Protocol::Wait; wait.delayMs=5;
  {
    fresh(); Engine e;
    add(0,mark(1));add(0,call(3));add(0,mark(5));
    add(3,mark(2));add(3,call(26));add(3,call(27));add(3,mark(4));
    add(27,mark(3));
    assert(e.trigger({0,0}));drain(e);
    assert(marks()==std::vector<int>({1,2,3,4,5}) && e.stats.events==1);
  }
  {
    fresh(); Engine e;
    for (uint8_t i=0;i<27;++i) {add(i,call(i+1));add(i,mark(i));}
    add(27,wait);add(27,mark(27));
    assert(e.trigger({0,0}));
    while (e.stats.accepted==0 && hostMillis<10) {++hostMillis;e.beginCycle();e.tick(hostMillis);}
    assert(e.stats.accepted==1 && e.stats.events==1 && e.activeCount()==1 && marks().empty());
    drain(e);std::vector<int> expected;for(int i=27;i>=0;--i)expected.push_back(i);
    assert(marks()==expected);
  }
  {
    fresh(); Engine e;
    for (int i=0;i<7;++i) add(0,start);
    add(0,call(2));add(0,mark(1));for(int i=0;i<7;++i)add(0,end);
    start.loopCount=1;for(int i=0;i<8;++i)add(2,start);for(int i=0;i<8;++i)add(2,end);
    assert(e.trigger({0,0}));drain(e);
    assert(marks()==std::vector<int>(128,1));
  }
  {
    fresh(); Engine e;start.loopCount=2;
    add(0,start);add(0,mark(1));add(0,call(2));add(0,mark(3));add(0,end);
    start.loopCount=3;add(2,start);add(2,mark(2));add(2,end);
    assert(e.trigger({0,0}));drain(e);
    assert(marks()==std::vector<int>({1,2,2,2,3,1,2,2,2,3}));
  }
  {
    fresh(); Engine e;start.loopCount=2;
    add(0,call(4));add(0,mark(10));add(2,call(4));add(2,mark(20));
    add(4,start);add(4,mark(4));add(4,wait);add(4,end);
    assert(e.trigger({0,0})); hostMillis=2;
    assert(e.trigger({2,2}) && e.activeCount()==2);
    drain(e); assert(marks()==std::vector<int>({4,4,4,4,10,20}));
  }
  std::cout<<"PASS Call Engine: frame boundaries, empty child, 27 returns, parent 7 / child 8, independent invocations\n";
}
