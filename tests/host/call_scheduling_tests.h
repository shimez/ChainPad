void callSchedulingTests() {
  auto fresh=[] { reset(); config=Config{}; hostMillis=0; fake::wifi=true; fake::packets.clear(); };
  auto set=[](uint8_t id,std::initializer_list<Action> actions) {
    auto& c=config.chains[id]; c.count=0; for(const auto& a:actions)c.actions[c.count++]=a;
  };
  auto prepare=[] {
    calls::Graph g; for(uint8_t i=0;i<INPUT_COUNT;++i)assert(g.add(i,config.chains[i])==calls::Error::None);
    calls::Cycle c;assert(g.validate(c)==calls::Error::None);config.loopReachable=g.loopReachable();
  };
  auto cycle=[](Engine& e,uint32_t now){hostMillis=now;e.beginCycle();backendsTick(now);e.tick(now);};
  auto call=[](uint8_t target){Action a;a.protocol=Protocol::CallChain;a.targetInput=target;return a;};
  auto osc=[](char label){ Action a;strcpy(a.address,"/x");a.address[1]=label;return a;};
  auto packets=[] {std::string labels;for(const auto& p:fake::packets)labels+=char(p.at(1));return labels;};
  Action wait;wait.protocol=Protocol::Wait;wait.delayMs=5;
  Action note;note.protocol=Protocol::Midi;note.transport=Transport::Ble;note.message=MidiMessage::NoteOnOff;note.delayMs=5;
  Action key;key.protocol=Protocol::Keyboard;key.transport=Transport::Ble;key.keyMessage=KeyMessage::DownUp;key.delayMs=5;key.usage=4;
  Action stop;stop.protocol=Protocol::StopOtherChains;
  Action start;start.protocol=Protocol::LoopStart;start.loopCount=1;
  Action end;end.protocol=Protocol::LoopEnd;

  for(auto tail:{wait,note,key}) {
    fresh();Engine e;set(0,{call(2),osc('p')});set(2,{tail});prepare();
    assert(e.triggerCallModelForTest({0,0}) && e.activeCount()==1);
    cycle(e,4);assert(packets().empty());
    if(tail.protocol!=Protocol::Wait) {
      // Time has elapsed, but the queued release hasn't reached the transport.
      fake::writable=false;cycle(e,5);assert(e.activeCount()==1 && packets().empty());
      fake::writable=true;
    }
    cycle(e,5);cycle(e,6);assert(!e.activeCount() && packets()=="p");
    if(tail.protocol==Protocol::Midi) assert(fake::midi.size()==2 && fake::midi[1].status==0x80);
    if(tail.protocol==Protocol::Keyboard) assert(fake::hid.front().keys[0]==4 && fake::hid.back().keys[0]==0);
  }
  for(bool panic:{false,true}) {
    fresh();Engine e;set(0,{call(2),osc('a')});set(2,{note});
    set(4,{call(6),osc('b')});set(6,{key});
    set(8,{call(10),osc('p')});set(10,{wait,stop,osc('s')});
    prepare();assert(e.triggerCallModelForTest({0,0}) && e.triggerCallModelForTest({4,0}));
    assert(fake::midi.size()==1 && fake::hid.back().keys[0]==4);
    if(panic) {
      e.cancelAll();backendsPanic();cycle(e,10);cycle(e,20);
      assert(!e.activeCount() && fake::midi.size()==1 && fake::hid.back().keys[0]==0 && packets().empty());
    } else {
      // Drive scheduler before backend release: both other jobs still pending.
      assert(e.triggerCallModelForTest({8,0}));hostMillis=5;e.beginCycle();e.tick(5);
      assert(e.stats.cancelledChains==2 && !e.activeCount() && packets()=="sp");
      cycle(e,6);cycle(e,12);
      assert(fake::midi.size()==2 && fake::midi.back().status==0x80 && fake::hid.back().keys[0]==0 && packets()=="sp");
    }
  }
  {
    fresh();Engine e;start.loopCount=10;auto mark=note;mark.message=MidiMessage::CC;mark.delayMs=0;
    for(uint8_t i=0;i<5;++i){mark.number=i;set(i*2,{start,call(10+i),end});set(10+i,{mark});}
    prepare();
    for(uint8_t i=0;i<5;++i) {
      assert(e.triggerCallModelForTest({uint8_t(i*2),0}));
      // Header then Call/CC/Return/Footer. Instruction 16 is the fourth Return.
      assert(fake::midi.size()==size_t(4*std::min<int>(i+1,4)));
    }
    e.tick(0);assert(fake::midi.size()==16 && e.activeCount()==5);
    for(unsigned t=1;t<=20 && e.activeCount();++t)cycle(e,t);
    assert(!e.activeCount() && fake::midi.size()==50);
    for(unsigned n=0;n<5;++n)assert(std::count_if(fake::midi.begin(),fake::midi.end(),
      [n](auto m){return m.a==n;})==10);
  }
  {
    fresh();Engine e;start.loopCount=1;set(0,{osc('a'),osc('a'),call(4)});set(2,{osc('b'),call(4)});
    set(4,{call(6)});set(6,{start,osc('c'),end});set(8,{call(10)});set(10,{osc('u'),osc('v')});prepare();
    assert(e.triggerCallModelForTest({0,0}) && e.triggerCallModelForTest({2,0}) && packets()=="a");
    assert(e.triggerCallModelForTest({8,0}) && packets()=="auv"); // loop-free bypass
    for(unsigned t=1;t<=20;++t) {
      cycle(e,t);
      assert(packets()==std::string("auvabcc").substr(0,3+std::min(t/4,4u)));
    }
    assert(!e.activeCount());
  }
  {
    fresh();Engine e;set(0,{wait});set(2,{osc('x'),call(4)});set(4,{stop});
    set(6,{osc('s'),stop,call(8),osc('p')});set(8,{start,osc('c'),end});prepare();
    for(unsigned i=0;i<32;++i)assert(e.trigger({0,0}));
    assert(!e.triggerCallModelForTest({2,0}) && packets().empty() && e.activeCount()==32);
    assert(e.triggerCallModelForTest({6,0}) && packets()=="sc" && e.activeCount()==1);
    cycle(e,3);assert(packets()=="sc");cycle(e,4);assert(packets()=="scp" && !e.activeCount() && e.stats.cancelledChains==32);
  }
  std::cout<<"PASS Call scheduling: terminal Wait/paired release, Stop/Panic, 16/64 budgets, transitive OSC FIFO and full-slot admission\n";
}
