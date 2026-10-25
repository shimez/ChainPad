#include "obs_test_console.h"
#include "obs_backend.h"
#include "obs_actions.h"
#include <cassert>
#include <iostream>
namespace chimera {
ObsSettings received;unsigned applies=0, sends=0, panics=0, drains=0;
bool obsApplySettings(const ObsSettings& s){received=s;++applies;return true;}
SendResult obsDispatch(const char*,uint32_t& id){id=++sends;return SendResult::Accepted;}
void obsPanic(){++panics;}
ObsStatus obsStatusSnapshot(){return {};}
bool obsNextResult(ObsResult&){++drains;return false;}
}
int main(){
  using namespace chimera;Print out;
  Action action; action.protocol=Protocol::Obs;
  assert(obsActionDispatch(action)==SendResult::Unavailable && !strcmp(obsActionStatus().admission,"testConsoleOwned"));
  obsActionsTick(); assert(drains==0 && sends==0);
  auto line=[&](const std::string& s){for(unsigned char c:s)assert(obsTestByte(c,out));};
  for(char c:std::string("mhd?p@"))assert(!obsTestByte(c,out));
  line("!{\"op\":\"settings\",\"ip\":\"192.0.2.1\",\"port\":4455,\"enabled\":true,\"password\":\"secret-mhd?\"}\r\n");
  assert(applies==1&&!strcmp(received.password,"secret-mhd?"));assert(out.output.find("secret")==std::string::npos);
  line("!{\"op\":\"scene\",\"name\":\"scene\",\"count\":16}\n");assert(sends==16);
  line("!{\"op\":\"scene\",\"name\":\"scene\",\"count\":17}\n");assert(sends==16);
  line("!{\"op\":\"settings\",\"ip\":\"192.0.2.1\",\"port\":4455,\"enabled\":true,\"password\":\"x\\u0000secret\"}\n");assert(applies==1);
  line("!"+std::string(1100,'m')+"\n");assert(applies==1&&sends==16);
  line("!secret");assert(obsTestByte(27,out));assert(!obsTestByte('m',out));
  line("!{\"op\":\"panic\"}\n");assert(panics==1);
  line("!{\"op\":\"status\"}\n");assert(out.output.find("secret")==std::string::npos);
  std::cout<<"PASS test console: default commands pass, single consumption, secret non-echo, bounded lines/bursts, NUL rejection, ESC recovery, Panic/status\n";
}
