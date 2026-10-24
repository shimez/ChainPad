#include "obs_protocol.h"
#include "obs_auth.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
using namespace chimera::obs;
static Received receive(Protocol& p, const char* text) { return p.receive(text, strlen(text), "test-password", authentication); }
int main() {
  Protocol p;
  assert(!p.request("scene", 1, 0)); p.connected();
  assert(receive(p, R"({"op":2,"d":{"negotiatedRpcVersion":1}})") == Received::Invalid);
  assert(receive(p, R"({"op":0,"d":{"rpcVersion":1,"authentication":{"salt":"test-salt","challenge":"test-challenge"}}})") == Received::Identify);
  JsonDocument identify; assert(!deserializeJson(identify, p.wire));
  assert(identify["op"] == 1 && identify["d"]["rpcVersion"] == 1 && identify["d"]["eventSubscriptions"] == 0);
  assert(identify["d"]["authentication"] == "zBIloly70ANx+T9nUkQVuTCqTUuHtfrqEdn8XDQ3m/s=");
  assert(!strstr(p.wire, "test-password"));
  assert(receive(p, R"({"op":0,"d":{"rpcVersion":1}})") == Received::Invalid);
  assert(receive(p, R"({"op":2,"d":{"negotiatedRpcVersion":1}})") == Received::Ready);
  assert(p.request(u8"ゲーム画面\"\\", 1, UINT32_MAX - 100));
  JsonDocument request; assert(!deserializeJson(request, p.wire));
  assert(request["d"]["requestId"] == "chainpad-1");
  assert(request["d"]["requestData"]["sceneName"] == u8"ゲーム画面\"\\");
  assert(!p.request("second", 2, 0));
  assert(receive(p, R"({"op":7,"d":{"requestId":"chainpad-2"}})") == Received::Ignored);
  assert(receive(p, R"({"op":7,"d":{"requestId":"chainpad-1\u0000bad"}})") == Received::Ignored);
  assert(receive(p, R"({"op":7,"d":{"requestId":"chainpad-1","requestType":"SetCurrentProgramScene","requestStatus":{"result":true,"code":100}}})") == Received::Result);
  assert(p.success && p.code == 100);
  assert(!p.expire(2898) && p.expire(2899)); p.pending = 0;
  assert(receive(p, R"({"op":7,"d":{"requestId":"chainpad-1"}})") == Received::Ignored);
  assert(p.request("missing", 3, 0));
  assert(receive(p, R"({"op":7,"d":{"requestId":"chainpad-3","requestType":"SetCurrentProgramScene","requestStatus":{"result":false,"code":600,"comment":"echo test-password"}}})") == Received::Result);
  assert(!p.success && p.code == 600); // No comment field is retained at all.
  assert(receive(p, R"({"op":7,"d":{"requestId":"chainpad-3","requestType":"Other","requestStatus":{"result":true,"code":100}}})") == Received::Invalid);
  assert(receive(p, "{}") == Received::Invalid);
  assert(receive(p, R"({"op":"0","d":{}})") == Received::Invalid);
  assert(receive(p, R"({"op":7,"d":[]})") == Received::Invalid);
  assert(p.receive(std::string(2049, ' ').c_str(), 2049, "", authentication) == Received::Invalid);
  p.connected();
  std::string many=R"({"op":0,"d":{"rpcVersion":1,"extra":[)";
  for(int i=0;i<900;++i)many+=(i?",0":"0");many+="]}}";
  assert(many.size()<2048&&receive(p,many.c_str())==Received::Invalid); // JSON budget, not wire limit.
  assert(receive(p, R"({"op":0,"d":{"rpcVersion":1,"authentication":{"salt":"s\u0000x","challenge":"c"}}})") == Received::Invalid);
  assert(receive(p, R"({"op":0,"d":{"rpcVersion":0}})") == Received::Invalid);
  assert(receive(p, R"({"op":0,"d":{"rpcVersion":1}})") == Received::Identify);
  assert(!deserializeJson(identify, p.wire) && identify["d"]["authentication"].isNull());
  assert(terminalClose(4009) && terminalClose(4010) && terminalClose(4011));
  assert(!terminalClose(1000) && !terminalClose(1006));
  assert(validText(u8"ゲーム", 9) && !validText(u8"ゲーム", 8));
  assert(!validText("\xc0\xaf", 4) && !validText("\xed\xa0\x80", 4) && !validText("\xf4\x90\x80\x80", 4));
  Assembly a; assert(!a.append(nullptr, 0, false, 0));
  auto text = std::string(2048, 'a');
  assert(a.append(reinterpret_cast<const uint8_t*>(text.data()), 1000, true, UINT32_MAX - 100));
  assert(!a.append(nullptr, 0, true, 0));
  assert(a.append(reinterpret_cast<const uint8_t*>(text.data()), 1048, false, 0));
  assert(!a.append(reinterpret_cast<const uint8_t*>(text.data()), 1, false, 0));
  assert(a.length == 2048 && a.bytes[2048] == 0);
  assert(!a.expired(4898) && a.expired(4899)); a.reset(); assert(!a.active && !a.length);
  std::cout << "PASS OBS protocol/auth: Hello ordering, reference vector, no-auth, UTF-8/escaping, IDs, errors, no server comments, timeout/wrap, limits, fragments, Close policy\n";
}
