#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
enum HTTPMethod { HTTP_GET, HTTP_PUT, HTTP_POST, HTTP_DELETE };
enum HTTPRawStatus { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };
struct HTTPRaw { HTTPRawStatus status; size_t currentSize=0, totalSize=0; uint8_t buf[256]{}; };
class WebServer;
class RequestHandler {
public:
  virtual ~RequestHandler()=default;
  virtual bool canHandle(WebServer&, HTTPMethod,const String&)=0;
  virtual bool canRaw(WebServer&,const String&)=0;
  virtual void raw(WebServer&,const String&,HTTPRaw&)=0;
  virtual bool handle(WebServer&,HTTPMethod,const String&)=0;
};
struct TestClient { bool stopped=false; void stop(){stopped=true;} };
class WebServer {
public:
  std::unique_ptr<RequestHandler> handler;
  TestClient socket;
  HTTPMethod method_=HTTP_GET;
  int length=0, code=0;
  std::map<std::string,std::string> requestHeaders, responseHeaders;
  std::map<std::string,std::string> arguments;
  // Match Arduino WebServer Parsing.cpp: the raw POST branch skips _parseArguments.
  String arg(const char* name){return method_==HTTP_POST && handler->canRaw(*this,"") ? String("") : String(arguments[name]);}
  std::string response;
  void collectHeaders(const char**,size_t){}
  void addHandler(RequestHandler* h){handler.reset(h);}
  TestClient& client(){return socket;}
  int clientContentLength(){return length;}
  HTTPMethod method(){return method_;}
  String header(const char* name){return requestHeaders[name];}
  bool hasHeader(const char* name){return requestHeaders.count(name)!=0;}
  void sendHeader(const char* name,const char* value){responseHeaders[name]=value;}
  void send(int status,const char*,const char* value){code=status;response=value;}
  void request(HTTPMethod method,const char* uri,const std::string& body="",const char* type="application/json",bool aborted=false){
    socket.stopped=false;code=0;response.clear();responseHeaders.clear();requestHeaders.clear();
    requestHeaders["Content-Type"]=type; method_=method;length=int(body.size());
    if(!handler->canHandle(*this,method,uri))return;
    if(method==HTTP_PUT||method==HTTP_POST){
      HTTPRaw raw{RAW_START};handler->raw(*this,uri,raw);
      for(size_t pos=0;pos<body.size()&&!socket.stopped;){
        raw.status=RAW_WRITE;raw.currentSize=std::min(size_t(256),body.size()-pos);
        memcpy(raw.buf,body.data()+pos,raw.currentSize);pos+=raw.currentSize;
        handler->raw(*this,uri,raw);
      }
      if(socket.stopped)return;
      raw.status=aborted?RAW_ABORTED:RAW_END;handler->raw(*this,uri,raw);
      if(aborted)return;
    }
    handler->handle(*this,method,uri);
  }
};
