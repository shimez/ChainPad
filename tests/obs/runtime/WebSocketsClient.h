#pragma once
#include "WiFi.h"
#include <deque>
#include <functional>
#include <cassert>
#include <thread>
enum WStype_t { WStype_CONNECTED, WStype_DISCONNECTED, WStype_TEXT, WStype_FRAGMENT_TEXT_START, WStype_FRAGMENT, WStype_FRAGMENT_FIN, WStype_PING, WStype_PONG };
enum WSopcode_t { WSop_close };
constexpr int WSC_NOT_CONNECTED=0,WSC_HEADER=1,WSC_CONNECTED=2;
struct WSclient_t { int status=0;WiFiClient* tcp=nullptr; };
namespace wire {
inline bool online=true, handshake=true, auth=true, failSend=false;
inline std::string headers;
inline uint16_t closeCode=0;
inline std::deque<std::pair<WStype_t,std::string>> rx;
inline std::vector<std::string> sent, hosts;
inline std::function<void()> beforeSend;
inline std::thread::id owner;
inline void assertOwner(){assert(owner==std::this_thread::get_id());}
inline void text(const std::string& s){rx.push_back({WStype_TEXT,s});}
}
class WebSocketsClient {
  std::function<void(WStype_t,uint8_t*,size_t)> callback;
  WiFiClient tcp;
  bool startPending=false;
protected:
  WSclient_t _client;
  unsigned _port=0;
  virtual void messageReceived(WSclient_t*,WSopcode_t,uint8_t*,size_t,bool) {}
  void clientDisconnect(WSclient_t*,const char*) {
    wire::assertOwner();_client.status=0;startPending=false;wire::rx.clear();
  }
  bool clientIsConnected(WSclient_t*) {wire::assertOwner();return wire::online;}
  void handleHeader(WSclient_t*,String*) {wire::assertOwner();}
public:
  WebSocketsClient(){wire::owner=std::this_thread::get_id();_client.tcp=&tcp;}
  void onEvent(std::function<void(WStype_t,uint8_t*,size_t)> fn){wire::assertOwner();callback=fn;}
  void begin(const char* ip,unsigned port,const char*,const char*){wire::assertOwner();wire::hosts.push_back(ip);_port=port;startPending=true;}
  void setReconnectInterval(unsigned) {wire::assertOwner();}
  void enableHeartbeat(unsigned,unsigned,unsigned) {wire::assertOwner();}
  void loop() {
    wire::assertOwner();
    if(startPending){
      if(!wire::online)return;
      startPending=false;
      if(!wire::headers.empty()){
        _client.status=WSC_HEADER;tcp.rx.assign(wire::headers.begin(),wire::headers.end());tcp.offset=0;return;
      }
      _client.status=WSC_CONNECTED;callback(WStype_CONNECTED,nullptr,0);
      if(wire::handshake)wire::text(wire::auth?R"({"op":0,"d":{"rpcVersion":1,"authentication":{"salt":"test-salt","challenge":"test-challenge"}}})":R"({"op":0,"d":{"rpcVersion":1}})");
    }
    if(wire::closeCode){
      uint8_t bytes[]={uint8_t(wire::closeCode>>8),uint8_t(wire::closeCode)};
      wire::closeCode=0;messageReceived(&_client,WSop_close,bytes,2,true);_client.status=0;callback(WStype_DISCONNECTED,nullptr,0);return;
    }
    if(!wire::rx.empty()){
      auto e=wire::rx.front();wire::rx.pop_front();callback(e.first,reinterpret_cast<uint8_t*>(e.second.data()),e.second.size());
    }
  }
  bool sendTXT(char* text) {
    wire::assertOwner();
    if(wire::beforeSend){auto fn=wire::beforeSend;wire::beforeSend={};fn();}
    wire::sent.push_back(text);
    if(wire::failSend)return false;
    if(strstr(text,"\"op\":1")){
      if(wire::auth&&!strstr(text,"zBIloly70ANx+T9nUkQVuTCqTUuHtfrqEdn8XDQ3m/s="))wire::closeCode=4009;
      else wire::text(R"({"op":2,"d":{"negotiatedRpcVersion":1}})");
    }
    return true;
  }
};
