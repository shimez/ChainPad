// Host-only comparison of the extracted production GET handler. The same
// fixture/transport adapter/compiler options are used for HEAD and worktree.
#include "model.h"
#include "json_wire.h"
#include "http_adapter.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace chimera;
namespace chimera { bool configCommittedApplyFailed(){return false;} } // Healthy GET benchmark, no storage linked.
HttpAdapter web;
bool resetGuard(){return false;}
void result(int,const String&){throw std::runtime_error("GET failed");}
void diagnosticCheckpoint(const char*){}
void setup(){
#include "get.inc"
}
int main(){
  setup();auto& c=activeConfig();
  for(int id=0;id<28;++id){auto& chain=c.chains[id];chain.count=id>=26?8:(id%2?0:16);
    for(int i=0;i<chain.count;++i){auto& a=chain.actions[i];a.protocol=Protocol::Osc;a.oscType=OscType::String;
      memset(a.address,'a',sizeof(a.address)-1);a.address[0]='/';a.address[sizeof(a.address)-1]=0;
      memset(a.stringValue,'s',sizeof(a.stringValue)-1);a.stringValue[sizeof(a.stringValue)-1]=0;}}
  c.encoderRotation.outputCount=16;
  for(auto& o:c.encoderRotation.outputs){memset(o.address,'r',sizeof(o.address)-1);o.address[0]='/';o.address[sizeof(o.address)-1]=0;}
  using Clock=std::chrono::steady_clock;Clock::time_point start;
  double headers=0,total=0;
  for(int i=0;i<5;++i)web.request("/api/config",HTTP_GET);
  web.beforeSend=[&](int code){if(code!=200)throw std::runtime_error("non200");headers+=std::chrono::duration<double,std::micro>(Clock::now()-start).count();};
  for(int i=0;i<100;++i){start=Clock::now();web.request("/api/config",HTTP_GET);total+=std::chrono::duration<double,std::micro>(Clock::now()-start).count();}
  std::cout<<"max224+16 GET n=100 header_us="<<headers/100<<" total_us="<<total/100<<" bytes="<<web.body.size()<<"\n";
}
