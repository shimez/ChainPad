#pragma once
#include <cstdlib>
#include <string>
#include <vector>
namespace jsonFault {
inline std::string target, wire;
inline int failAt=0, calls=0, failures=0, matched=0, allocationCount=0;
inline bool enabled=false, overflow=false;
inline int occurrence=1;
inline bool continuous=false;
inline std::vector<size_t> sizes;
struct Allocator : ArduinoJson::Allocator {
  bool reject(size_t n){if(!enabled)return false;sizes.push_back(n);++calls;if(failAt>0&&(calls==failAt||(continuous&&calls>=failAt))){++failures;return true;}return false;}
  void* allocate(size_t n)override{return reject(n)?nullptr:std::malloc(n);}
  void deallocate(void* p)override{std::free(p);}
  void* reallocate(void* p,size_t n)override{return reject(n)?nullptr:std::realloc(p,n);}
};
inline Allocator allocator;
inline void arm(const char* kind,int allocation,int invocation=1,bool persistent=false){target=kind;failAt=allocation;occurrence=invocation;continuous=persistent;calls=failures=matched=allocationCount=0;enabled=target=="all";overflow=false;wire.clear();sizes.clear();}
inline bool enter(const char* kind){if(target!=kind&&!(target=="request"&&std::string(kind)=="network"))return false;if(++matched!=occurrence)return false;enabled=true;return true;}
}
// Only the test include path substitutes default documents. ArduinoJson itself
// is the installed, unmodified library, with its public Allocator interface.
class FaultDocument : public ArduinoJson::JsonDocument {
public:
  FaultDocument():ArduinoJson::JsonDocument(&jsonFault::allocator){}
};
#define JsonDocument FaultDocument
