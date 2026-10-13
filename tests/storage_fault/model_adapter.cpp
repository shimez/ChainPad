// Compile unchanged model implementation with renamed encode entry points;
// wrappers scope allocator failure to one actual production encode invocation.
#define encodeChain rawEncodeChain
#define encodeRotation rawEncodeRotation
#include "../../src/model.cpp"
#undef encodeChain
#undef encodeRotation
#include "../../src/json_wire.h"
namespace {
void observe(bool selected, JsonDocument& doc){
  if(!selected)return;
  jsonFault::enabled=false;jsonFault::allocationCount=jsonFault::calls;
  jsonFault::overflow=doc.overflowed();
  String s;chimera::serializeSettingsJson(doc,s);jsonFault::wire=s.c_str();
}
}
namespace chimera {
void encodeChain(const Chain& c,uint8_t id,JsonDocument& d){bool s=jsonFault::enter(id==0?"chain":"other");rawEncodeChain(c,id,d);observe(s,d);}
void encodeChain(const ChainView& c,uint8_t id,JsonDocument& d){bool s=jsonFault::enter(id==0?"chain":"other");rawEncodeChain(c,id,d);observe(s,d);}
void encodeRotation(const EncoderRotationSettings& r,JsonDocument& d,bool lossless){bool s=jsonFault::enter("rotation");rawEncodeRotation(r,d,lossless);observe(s,d);}
}
