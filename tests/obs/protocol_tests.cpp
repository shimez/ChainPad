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
  static_assert(sizeof(SceneList) == 4172, "Discovery stays within the existing two-slot pool");
  {
    Protocol p; p.state=State::Ready; SceneList list; p.scenes=&list;
    Command c; c.operation=Operation::SetSceneItemVisibility; strcpy(c.target,"group"); c.itemId=ITEM_ID_LIMIT;
    assert(p.request(c,1,0));JsonDocument sent;deserializeJson(sent,p.wire);
    assert(sent["d"]["requestData"]["sceneItemId"].as<uint64_t>()==ITEM_ID_LIMIT);
    assert(sent["d"]["requestData"]["sceneItemEnabled"]==true);
    p.pending=0;c.visibility=Visibility::Toggle;assert(!p.request(c,2,0));
    c.operation=Operation::GetSceneItemEnabled;assert(p.request(c,2,0));
    assert(receive(p,R"({"op":7,"d":{"requestId":"chainpad-2","requestType":"GetSceneItemEnabled","requestStatus":{"result":true,"code":100},"responseData":{"sceneItemEnabled":false}}})")==Received::Result && p.success && !p.itemEnabled);
    assert(receive(p,R"({"op":7,"d":{"requestId":"chainpad-2","requestType":"GetSceneItemEnabled","requestStatus":{"result":true,"code":100},"responseData":{"sceneItemEnabled":0}}})")==Received::Result && !p.success);
    auto fetch=[&](Operation op,const char* field,const std::string& entries){
      p.pending=0;c={};c.operation=op;if(itemList(op))strcpy(c.target,"group");assert(p.request(c,3,0));
      std::string wire=std::string("{\"op\":7,\"d\":{\"requestId\":\"chainpad-3\",\"requestType\":\"")+requestType(op)+"\",\"requestStatus\":{\"result\":true,\"code\":100},\"responseData\":{\""+field+"\":"+entries+"}}}";
      return p.receive(wire.c_str(),wire.size(),"",authentication);
    };
    assert(fetch(Operation::GetGroupList,"groups",R"(["g1","g2"])")==Received::Result&&p.success&&list.count==2);
    for(auto op:{Operation::GetSceneItemList,Operation::GetGroupSceneItemList}){
      assert(fetch(op,"sceneItems",R"([{"sourceName":"same","sceneItemId":0},{"sourceName":"same","sceneItemId":9007199254740991}])")==Received::Result&&p.success);
      assert(list.count==2 && list.itemId(0)==0 && list.itemId(1)==ITEM_ID_LIMIT);
      assert(fetch(op,"sceneItems",R"([{"sourceName":"a","sceneItemId":0},{"sourceName":"b","sceneItemId":0}])")==Received::Result&&!p.success&&list.count==0);
      for(const auto& id:{"-1","0.5","true","null","\"12\"","9007199254740992"}){
        assert(fetch(op,"sceneItems",std::string("[{\"sourceName\":\"x\",\"sceneItemId\":")+id+"}]")==Received::Result&&!p.success);
      }
      std::string dense="{";for(int i=0;i<1000;++i)dense+=(i?",":"")+std::string("\"x")+std::to_string(i)+"\":0";dense+="}";
      assert(fetch(op,"sceneItems","[{\"sourceName\":\"x\",\"sceneItemId\":1,\"sceneItemTransform\":"+dense+"}]")==Received::Result&&p.success);
      std::string many="[";for(int i=0;i<33;++i)many+=(i?",":"")+std::string("{\"sourceName\":\"x\",\"sceneItemId\":")+std::to_string(i)+"}";many+="]";
      assert(fetch(op,"sceneItems",many)==Received::Result&&!p.success);
      assert(fetch(op,"sceneItems","[]")==Received::Result&&p.success&&list.count==0);
      p.pending=0;assert(p.request(c,4,0));
      std::string boundary=std::string("{\"op\":7,\"d\":{\"requestId\":\"chainpad-4\",\"requestType\":\"")+requestType(op)+"\",\"requestStatus\":{\"result\":true,\"code\":100},\"responseData\":{\"sceneItems\":[{\"sourceName\":\"x\",\"sceneItemId\":1,\"unused\":\"";
      const std::string suffix="\"}]}}}";boundary+=std::string(MESSAGE_LIMIT-boundary.size()-suffix.size(),'x');boundary+=suffix;
      assert(boundary.size()==MESSAGE_LIMIT&&receive(p,boundary.c_str())==Received::Result&&p.success);
      boundary+=' ';assert(receive(p,boundary.c_str())==Received::Invalid);
    }
    std::cout<<"PASS item protocol: safe IDs, strict boolean, groups, duplicate names/IDs, filtered dense transforms and bounded lists\n";
  }
  {
    Protocol discovery;SceneList list;discovery.scenes=&list;discovery.state=State::Ready;
    Command command;command.operation=Operation::GetSceneList;
    auto fetch=[&](unsigned count,unsigned length){
      discovery.pending=0;assert(discovery.request(command,99,0));
      JsonDocument reply;reply["op"]=7;auto d=reply["d"].to<JsonObject>();
      d["requestId"]="chainpad-99";d["requestType"]="GetSceneList";
      d["requestStatus"]["result"]=true;d["requestStatus"]["code"]=100;
      auto names=d["responseData"]["scenes"].to<JsonArray>();
      for(unsigned i=0;i<count;++i){auto scene=names.add<JsonObject>();scene["sceneName"]=std::string(length,'a');scene["sceneUuid"]=std::string(36,'u');scene["sceneIndex"]=i;}
      std::string wire;serializeJson(reply,wire);return receive(discovery,wire.c_str());
    };
    assert(fetch(32,100)==Received::Result && discovery.success && list.count==32 && list.used==3232);
    assert(fetch(0,0)==Received::Result && discovery.success && list.count==0);
    assert(fetch(33,10)==Received::Result && !discovery.success && discovery.code==-10);
    assert(fetch(32,192)==Received::Result && !discovery.success && discovery.code==-10);
    assert(fetch(1,193)==Received::Result && !discovery.success);
  }
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
  auto text = std::string(MESSAGE_LIMIT, 'a');
  assert(a.append(reinterpret_cast<const uint8_t*>(text.data()), 1000, true, UINT32_MAX - 100));
  assert(!a.append(nullptr, 0, true, 0));
  assert(a.append(reinterpret_cast<const uint8_t*>(text.data()), MESSAGE_LIMIT-1000, false, 0));
  assert(!a.append(reinterpret_cast<const uint8_t*>(text.data()), 1, false, 0));
  assert(a.length == MESSAGE_LIMIT && a.bytes[MESSAGE_LIMIT] == 0);
  assert(!a.expired(4898) && a.expired(4899)); a.reset(); assert(!a.active && !a.length);
  for(unsigned i=0;i<7;++i) {
    Protocol typed; typed.state=State::Ready; Command c; c.operation=Operation(i);
    if(named(c.operation)) { std::string boundary; for(int n=0;n<64;++n)boundary+=u8"名"; strcpy(c.target,boundary.c_str()); }
    c.muted=c.operation==Operation::SetInputMute;
    assert(typed.request(c,42,0));
    auto reply=[&](const char* type,const char* id){return std::string("{\"op\":7,\"d\":{\"requestId\":\"")+id+"\",\"requestType\":\""+type+"\",\"requestStatus\":{\"result\":false,\"code\":600}}}";};
    assert(receive(typed,reply(requestType(c.operation),"chainpad-43").c_str())==Received::Ignored);
    assert(receive(typed,reply(requestType(Operation((i+1)%7)),"chainpad-42").c_str())==Received::Invalid);
    assert(receive(typed,reply(requestType(c.operation),"chainpad-42").c_str())==Received::Result);
    assert(!typed.success && typed.code==600);
    typed.pending=0;
    if(named(c.operation)) { memset(c.target,'x',sizeof(c.target)); assert(!typed.request(c,43,0)); }
    else {strcpy(c.target,"unexpected");assert(!typed.request(c,43,0));}
  }
  std::cout << "PASS OBS protocol/auth: Hello ordering, reference vector, no-auth, UTF-8/escaping, IDs, errors, no server comments, timeout/wrap, limits, fragments, Close policy\n";
}
