// Compile unchanged model implementation with renamed encode entry points;
// wrappers scope allocator failure to one actual production encode invocation.
#define encodeChain rawEncodeChain
#define encodeRotation rawEncodeRotation
#define encodeNetwork rawEncodeNetwork
#define encodeNetworkPublic rawEncodeNetworkPublic
#include "../../src/model.cpp"
#undef encodeChain
#undef encodeRotation
#undef encodeNetwork
#undef encodeNetworkPublic
#include "../../src/json_wire.h"
namespace {
void observe(bool selected, JsonDocument& doc){
  if(!selected)return;
  // Wi-Fi prefix test keeps failures enabled through credential replacement
  // and request deep-copy, then disables at the first checked serialization.
  if(jsonFault::target=="request")return;
  jsonFault::enabled=false;jsonFault::allocationCount=jsonFault::calls;
  jsonFault::overflow=doc.overflowed();
  String s;chimera::serializeSettingsJson(doc,s);jsonFault::wire=s.c_str();
}
}
namespace chimera {
bool encodeNetwork(const NetworkSettings& n,JsonDocument& d){bool s=jsonFault::enter("network");bool ok=rawEncodeNetwork(n,d);observe(s,d);return ok;}
bool encodeNetworkPublic(const NetworkSettings& n,JsonDocument& d,bool wifiOnly){bool s=jsonFault::enter("network");bool ok=rawEncodeNetworkPublic(n,d,wifiOnly);observe(s,d);return ok;}
bool encodeChain(const Chain& c,uint8_t id,JsonDocument& d){bool s=jsonFault::enter(id==0?"chain":"other");bool ok=rawEncodeChain(c,id,d);observe(s,d);return ok;}
bool encodeChain(const ChainView& c,uint8_t id,JsonDocument& d){bool s=jsonFault::enter(id==0?"chain":"other");bool ok=rawEncodeChain(c,id,d);observe(s,d);return ok;}
bool encodeRotation(const EncoderRotationSettings& r,JsonDocument& d,bool lossless){bool s=jsonFault::enter("rotation");bool ok=rawEncodeRotation(r,d,lossless);observe(s,d);return ok;}
}
