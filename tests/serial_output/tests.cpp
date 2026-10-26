#include "serial_output.h"
#include "obs_test_console.h"
#include "obs_backend.h"
#include "hid_trace.h"
#include <cassert>
#include <climits>
#include <deque>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <new>
static size_t allocationCalls = 0;
void* operator new(size_t size) { ++allocationCalls; if (void* p=std::malloc(size ? size : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
// Exercise the real diagnostic implementation and its fixed record limits.
#include "../../src/diagnostics.cpp"

namespace chimera {
Config config;
Engine engine;
RotationRuntime rotationRuntime;
RotationSendStats rotationSendStats;
bool configStorageMounted() { return true; }
const char* configStorageStateName() { return "unsupported-version"; }
unsigned rotationPendingCount() { return UINT_MAX; }
size_t rotationPendingBytes() { return UINT32_MAX; }
size_t rotationSenderBytes() { return UINT32_MAX; }
BackendStatus backendStatus() { return {true,true,true,true,true,UINT32_MAX,UINT32_MAX}; }
uint32_t inputOverflows() { return UINT32_MAX; }
unsigned applies = 0, sends = 0, panics = 0, takes = 0;
std::deque<ObsResult> results;
bool obsApplySettings(const ObsSettings&) { ++applies; return true; }
SendResult obsDispatch(const char*, uint32_t& id) { ++sends; id = UINT32_MAX; return SendResult::Accepted; }
void obsPanic() { ++panics; }
ObsSceneMemory obsSceneMemorySnapshot() { return {}; }
ObsStatus obsStatusSnapshot() {
  ObsStatus s;
  s.state = obs::State::Ready; s.lastError = INT_MIN;
  s.lastClose = s.attempts = s.queued = s.accepted = s.rejected = s.completed = s.failed = s.discarded = s.resultDrops = UINT32_MAX;
  s.stackLowBytes = s.loopMaxUs = s.connectMs = s.authMs = s.responseMs = UINT32_MAX;
  return s;
}
bool obsNextResult(ObsResult& r) { ++takes; if (results.empty()) return false; r=results.front(); results.pop_front(); return true; }
}
using namespace chimera;
using serialout::Output;
struct Sink : Print {
  size_t limit = 64, calls = 0, maximum = 0;
  size_t write(const uint8_t* data, size_t size) override {
    ++calls; maximum = std::max(maximum, size); serialClockUs += 7;
    const size_t n = std::min(size, limit); output.append(reinterpret_cast<const char*>(data), n); return n;
  }
};
void put(Output& out, const std::string& s) {
  assert(out.begin(s.size())); assert(out.write(reinterpret_cast<const uint8_t*>(s.data()),s.size())==s.size()); assert(out.commit());
}
void drain(Output& out, Sink& sink) { for(unsigned i=0;i<5000;++i) out.pump(sink,true); assert(out.stats().queuedBytes==0); }
void reset() { auto& out=serialout::output(); out.cancel(); out=Output{}; }
void command(const std::string& line, Print& sink) { for(unsigned char c:line) assert(obsTestByte(c,sink)); }
void pass(const char* id) { std::cout << "PASS " << id << '\n'; }
int main() {
  {
    Output out; Sink sink; put(out,"first\nsecond\n"); drain(out,sink); assert(sink.output=="first\nsecond\n"); pass("T01 full write");
  }
  {
    Output out; Sink sink; sink.limit=7; std::string expected(800,'a'); expected+="\nOBS\nDIAG\n";
    put(out,expected); drain(out,sink); assert(sink.output==expected&&out.stats().txPartial>0); pass("T02 partial write");
  }
  {
    Output out; Sink sink; sink.limit=0; put(out,"retained\n");
    out.pump(sink,true); assert(out.stats().txZero==1&&out.stats().queuedBytes==9);
    sink.limit=64; drain(out,sink); assert(sink.output=="retained\n"); pass("T03 zero write");
  }
  {
    Output out; Sink sink; put(out,"retained\n");
    for(unsigned i=0;i<10000;++i)out.pump(sink,false);
    assert(sink.calls==0&&out.stats().queuedBytes==9); drain(out,sink); assert(sink.output=="retained\n");
    put(out,"after reconnect\n"); out.pump(sink,false); sink.limit=3; drain(out,sink);
    assert(sink.output.find("SERIAL session resumed;")!=std::string::npos);
    assert(sink.output.substr(sink.output.size()-16)=="after reconnect\n"); pass("T04 stop/resume and reconnect boundary");
  }
  {
    Output out; Sink sink; put(out,std::string(4096,'F')); assert(!out.begin(1));
    assert(out.stats().outputRejected==1&&out.stats().queuedBytes==4096); drain(out,sink);
    assert(sink.output.substr(0,4096)==std::string(4096,'F')); pass("T05 full buffer");
  }
  {
    Output out; Sink sink; std::string expected;
    for(unsigned i=0;i<20;++i) { std::string line=i%2?"DIAG part1 part2\n":"OBS status result\n"; put(out,line); expected+=line; }
    sink.limit=3; drain(out,sink); assert(sink.output==expected); pass("T06 producer ordering");
  }
  {
    reset(); Sink sink; auto& out=serialout::output(); results={{1,true,100},{2,false,INT_MIN}}; takes=0;
    put(out,std::string(4096,'F')); command("!{\"op\":\"status\"}\n",sink);
    assert(takes==0&&results.size()==2&&out.stats().resultDeferred==1); drain(out,sink);
    command("!{\"op\":\"status\"}\n",sink); drain(out,sink);
    assert(results.empty()&&sink.output.find("result id=2 success=0 code=-2147483648")!=std::string::npos);
    // Status fits its reservation, but results do not: only reserved results may be consumed.
    reset(); sink.output.clear(); put(out,std::string(4096-768,'P'));
    results.assign(8,{UINT32_MAX,false,INT_MIN}); takes=0;
    command("!{\"op\":\"status\"}\n",sink);
    assert(takes<8&&takes==8-results.size()&&out.stats().resultDeferred==1);
    drain(out,sink); results.clear(); pass("T07 result FIFO reservation");
  }
  {
    Output out; Sink sink; put(out,std::string(4000,'a')); drain(out,sink);
    put(out,std::string(500,'b')); drain(out,sink);
    assert(sink.output==std::string(4000,'a')+std::string(500,'b'));
    put(out,std::string(3500,'c')); assert(out.begin(500)); out.print("cancel wrap"); out.cancel();
    put(out,std::string(500,'d')); drain(out,sink);
    assert(sink.output==std::string(4000,'a')+std::string(500,'b')+std::string(3500,'c')+std::string(500,'d'));
    pass("T08 ring wrap including cancellation");
  }
  {
    reset(); Sink sink; auto& out=serialout::output(); put(out,"committed\n");
    assert(out.begin(20)); out.print("invisible"); out.pump(sink,true); assert(sink.calls==0);
    outputPrintf(out,"%s",std::string(600,'X').c_str()); assert(!out.commit());
    assert(out.stats().formatRejected==1); drain(out,sink);
    assert(sink.output.find("invisible")==std::string::npos&&sink.output.find('X')==std::string::npos);
    assert(out.begin(4)); out.print("abcde"); assert(!out.commit());
    assert(out.begin(10)); out.print("cancelled"); out.cancel();
    assert(out.begin(10)); assert(!out.begin(1)); out.print("last\n"); assert(out.commit());
    drain(out,sink); assert(sink.output.find("cancelled")==std::string::npos); pass("T09 format/overflow/cancel/nesting");
    assert(sink.output.find("committed\n")==0&&sink.output.find("last\n")!=std::string::npos); pass("T10 recovery after refusal");
  }
  {
    Output out; Sink sink; put(out,std::string(300,'p'));
    for(unsigned i=0;i<20;++i) { auto calls=sink.calls; out.pump(sink,true); assert(sink.calls-calls<=1); }
    assert(sink.maximum<=64&&out.stats().pumpMaxUs==7); pass("T11 pump byte/call/time limits");
  }
  {
    reset(); Sink sink; auto& out=serialout::output(); hostMillis=UINT32_MAX;
    for(auto& chain:config.chains) chain.count=16;
    engine.stats.rejectedChains=UINT32_MAX;
    rotationSendStats={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,{UINT32_MAX,UINT32_MAX,UINT32_MAX},{UINT32_MAX,UINT32_MAX,UINT32_MAX}};
    std::string reason(64,'r'), boot(160,'b');
    assert(serialReport(sink,2560,[&](Print& p){
      outputPrintf(p,"inputsReady=%u\nBoot: %s\nWi-Fi mode=%u status=%u AP=%s STA=%s\n",1,boot.c_str(),3,254,"255.255.255.255","255.255.255.255");
      printDiagnostics(p,reason.c_str());
    }));
    const auto m=out.stats().queuedBytes; drain(out,sink);
    // Exercise real maximum record count, bounded long phase/API strings, 32-bit limits.
    static const std::string phase(32,'p'), function(64,'f');
    startupCount=16; failureCount=UINT_MAX;
    for(auto& s:startup)s={phase.c_str(),UINT32_MAX,UINT32_MAX,UINT32_MAX};
    for(auto& f:failures)f={phase.c_str(),function.c_str(),UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    assert(serialReport(sink,3072,[](Print& p){printMemoryDiagnostics(p);}));
    const auto h=out.stats().queuedBytes; drain(out,sink);
    command("!{\"op\":\"status\"}\n",sink); const auto status=out.stats().queuedBytes; assert(status<=768); drain(out,sink);
    command("!{\"op\":\"scene\",\"name\":\"scene\",\"count\":16}\n",sink);
    const auto burst=out.stats().queuedBytes; assert(burst==1520); drain(out,sink);
    assert(serialReport(sink,64,[](Print& p){outputPrintf(p,"OBS TEST result id=%lu success=%u code=%d\n",ULONG_MAX,1,INT_MIN);}));
    const auto result=out.stats().queuedBytes; drain(out,sink);
    // Maximum serial statistics still fit the status budget (replace the zero-valued stats contribution).
    serialout::Stats maxStats{UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,4096,4096,UINT32_MAX};
    assert(serialReport(sink,768,[&](Print& p){serialout::printStats(p,maxStats);}));
    const auto statsMax=out.stats().queuedBytes; drain(out,sink);
    assert(status+81<=768); // Nine counters, at most nine extra decimal digits each.
    const auto errors=out.stats().formatRejected;
    assert(!serialReport(sink,2560,[&](Print& p){printDiagnostics(p,std::string(600,'r').c_str());}));
    assert(out.stats().formatRejected==errors+1);
    drain(out,sink); put(out,std::string(4096,'q')); const auto queries=heapQueries;
    assert(!serialReport(sink,2560,[](Print& p){printDiagnostics(p,"must-not-collect");}));
    assert(queries==heapQueries); drain(out,sink);
    std::cout<<"Measured bytes: m="<<m<<" h="<<h<<" status="<<status<<" burst="<<burst<<" result="<<result<<" statsMax="<<statsMax<<'\n';
    pass("T12 production diagnostic sizes / oversized strings / skip collection");
  }
  {
    reset(); Sink sink; auto& out=serialout::output(); put(out,std::string(4096,'f'));
    const auto a=applies,s=sends,p=panics;
    command("!{\"op\":\"settings\",\"enabled\":true,\"ip\":\"192.0.2.1\",\"port\":4455,\"password\":\"secret\"}\n",sink);
    command("!{\"op\":\"scene\",\"name\":\"scene\",\"count\":16}\n",sink);
    assert(applies==a+1&&sends==s+16&&out.stats().commandReplyDrops==2); pass("T13 operations despite output saturation");
    command("!{\"op\":\"panic\"}\n",sink); assert(panics==p+1&&out.stats().commandReplyDrops==3);
    drain(out,sink); assert(applies==a+1&&sends==s+16&&panics==p+1&&sink.output.find("secret")==std::string::npos); pass("T14 Panic independent of output");
  }
  {
    reset(); Sink sink; auto& out=serialout::output();
    hidtrace::command('2'); hidtrace::record(hidtrace::Input,0,1,1,1,1); hidtrace::command('0'); hidtrace::command('p');
    put(out,std::string(4096,'F')); hidtrace::dumpTick(sink); assert(out.stats().outputRejected==1);
    drain(out,sink); sink.output.clear();
    for(unsigned i=0;i<400;++i) {hidtrace::dumpTick(sink); out.pump(sink,true);}
    drain(out,sink);
    assert(sink.output.find("TRACE mode=2")==0&&sink.output.find("TRACE mode=2",1)==std::string::npos);
    assert(sink.output.find("TRACE END")!=std::string::npos&&sink.output.find(",input,0,1,1,1,1")!=std::string::npos); pass("T15 trace retry without skipping/duplicating");
  }
  {
    reset(); Sink sink; sink.limit=0; auto& out=serialout::output(); put(out,std::string(4096,'L'));
    const auto objectBytes=sizeof(out);
    const auto allocations=allocationCalls;
    for(unsigned i=0;i<100000;++i) { assert(!out.begin(256)); out.pump(sink,true); }
    assert(allocationCalls==allocations);
    assert(out.stats().queuedBytes==4096&&out.stats().maxQueuedBytes==4096&&sizeof(out)==objectBytes);
    assert(out.stats().outputRejected==100000&&out.stats().txZero==100000); pass("T16 long stopped receiver, bounded storage");
    sink.limit=64; drain(out,sink);
    const auto pos=sink.output.find("SERIAL output loss:"); assert(pos==4096);
    assert(sink.output.find("SERIAL output loss:",pos+1)==std::string::npos&&out.stats().outputRejected==100000);
    reset(); sink.output.clear(); put(out,std::string(4096,'S'));
    bool admitted=false;
    for(unsigned i=0;i<8&&!admitted;++i) {
      admitted=out.begin(384);
      if(admitted) {out.print("large reservation recovered\n");assert(out.commit());}
      out.pump(sink,true);
    }
    assert(admitted); drain(out,sink); pass("T17 coalesced notice without recursion/starvation");
  }
  {
    reset(); Sink sink; auto& out=serialout::output(); put(out,"before trace\n"); hidtrace::command('2');
    for(unsigned i=0;i<100000;++i) { hidtrace::record(hidtrace::Input,0,1,i,0,1); out.pump(sink,true,hidtrace::active()); }
    assert(sink.calls==0&&out.stats().queuedBytes==13);
    hidtrace::command('0'); out.pump(sink,true,hidtrace::active()); assert(sink.output=="before trace\n"); pass("T18 long trace pause and resume");
  }
}
