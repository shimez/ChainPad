// Actual patched upstream parser, with only Arduino/TCP replaced.
#include <WebSockets.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
struct Event { unsigned opcode; bool fin; std::string payload; };
class Harness : public WebSockets {
public:
  WiFiClient tcp;
  WSclient_t client{};
  std::vector<Event> events;
  bool disconnected = false;
  Harness() { client.tcp = &tcp; client.cIsClient = true; headerDone(&client); }
  void feed(const std::vector<uint8_t>& bytes) {
    tcp.rx.insert(tcp.rx.end(), bytes.begin(), bytes.end());
    while (tcp.available() && !disconnected) handleWebsocket(&client);
  }
  void clientDisconnect(WSclient_t*) override { disconnected = true; tcp.open = false; }
  bool clientIsConnected(WSclient_t*) override { return tcp.open; }
  void messageReceived(WSclient_t*, WSopcode_t opcode, uint8_t* bytes, size_t size, bool fin) override {
    assert(!disconnected);
    events.push_back({unsigned(opcode),fin,size ? std::string(reinterpret_cast<char*>(bytes), size) : ""});
  }
};
std::vector<uint8_t> frame(unsigned opcode, bool fin, const std::string& text) {
  std::vector<uint8_t> bytes{uint8_t(opcode | (fin ? 128 : 0))};
  if (text.size() < 126) bytes.push_back(uint8_t(text.size()));
  else { bytes.push_back(126); bytes.push_back(uint8_t(text.size() >> 8)); bytes.push_back(uint8_t(text.size())); }
  bytes.insert(bytes.end(), text.begin(), text.end()); return bytes;
}
int main() {
  static_assert(WEBSOCKETS_MAX_DATA_SIZE == 2048);
  Harness normal; normal.feed(frame(1, true, "hello"));
  assert(normal.events.size() == 1 && normal.events[0].payload == "hello" && !normal.disconnected);
  normal.feed(frame(9, true, "ping"));
  assert(normal.events.back().opcode == 9 && (normal.tcp.tx[0] & 15) == 10 && (normal.tcp.tx[1] & 128));
  normal.feed(frame(1, false, "first")); normal.feed(frame(0, false, "middle")); normal.feed(frame(0, true, "last"));
  assert(normal.events[2].payload == "first" && !normal.events[2].fin);
  assert(normal.events[3].opcode == 0 && !normal.events[3].fin);
  assert(normal.events[4].payload == "last" && normal.events[4].fin);
  normal.feed(frame(1, true, std::string(2048, 'x')));
  assert(normal.events.back().payload.size() == 2048 && !normal.disconnected);
  for (unsigned code : {1000u, 4009u, 4010u, 4011u}) {
    Harness close;
    std::string payload; payload += char(code >> 8); payload += char(code); payload += "reason";
    close.feed(frame(8, true, payload));
    assert(close.disconnected && close.events.size() == 1 && close.events[0].opcode == 8);
    assert(close.events[0].payload == payload && (close.tcp.tx[0] & 15) == 8);
  }
  Harness emptyClose; emptyClose.feed(frame(8, true, ""));
  assert(emptyClose.disconnected && emptyClose.events[0].payload.empty());
  Harness large; large.feed({0x81,126,8,1});
  assert(large.disconnected && large.events.empty() && (large.tcp.tx[0] & 15) == 8);
  Harness partial; partial.feed({0x81,5,'h'}); assert(partial.disconnected && partial.events.empty());
  std::cout << "PASS patched WebSockets wire parser: text, ping/pong masking, continuation, boundary, oversize, truncated payload, raw Close before teardown\n";
}
