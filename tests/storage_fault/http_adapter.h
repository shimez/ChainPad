#pragma once
#include <functional>
#include <map>
#include <string>
#include "Arduino.h"
constexpr int HTTP_GET=0,HTTP_PUT=1,HTTP_POST=2,CONTENT_LENGTH_UNKNOWN=-1;
struct HttpArg : String {
  explicit HttpArg(const std::string& s):String(s){}
  long toInt()const{return std::strtol(c_str(),nullptr,10);}
};
struct HttpAdapter {
  std::map<std::pair<std::string,int>,std::function<void()>> handlers;
  std::map<std::string,std::string> args;
  int status=0;std::string body;
  std::function<void(int)> beforeSend;
  void on(const char* path,int method,std::function<void()> fn){handlers[{path,method}]=fn;}
  HttpArg arg(const char* key){return HttpArg(args[key]);}
  void sendHeader(const char*,const char*){}
  void setContentLength(int){}
  void send(int code,const char*,const String& text){if(beforeSend)beforeSend(code);status=code;body=text.c_str();}
  void sendContent(const String& text){body+=text.c_str();}
  void request(const char* path,int method,const std::string& plain="",uint32_t token=0){
    status=0;body.clear();args={{"plain",plain},{"token",std::to_string(token)}};
    handlers.at({path,method})();
  }
};
