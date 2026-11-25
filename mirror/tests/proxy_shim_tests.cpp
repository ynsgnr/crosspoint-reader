// Host tests for the pure part of the phone proxy shim (mirror/core/ProxyShim.*).
#include <cstdio>
#include <cstring>

#include "core/ProxyShim.h"

using namespace mirror;

#define PCHECK(c)                                              \
  do {                                                         \
    ++checks;                                                  \
    if (!(c)) {                                                \
      ++failed;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                          \
  } while (0)

static ConnectReply reply(const char* s) { return parseConnectReply(s, std::strlen(s)); }

int runProxyShimTests(int& checks) {
  int failed = 0;
  {  // fake addresses
    FakeHostTable t;
    const uint32_t a = t.assign("opds.example.org");
    PCHECK(a == FakeHostTable::kBase + 1 && FakeHostTable::isFake(a));
    PCHECK(t.assign("opds.example.org") == a);  // stable while in the table
    const uint32_t b = t.assign("sync.example.org");
    PCHECK(b == a + 1 && *t.hostFor(a) == "opds.example.org" && *t.hostFor(b) == "sync.example.org");
    PCHECK(t.hostFor(FakeHostTable::kBase + 20) == nullptr && t.hostFor(0x0A000001) == nullptr);
    PCHECK(!FakeHostTable::isFake(FakeHostTable::kBase) &&
           !FakeHostTable::isFake(FakeHostTable::kBase + FakeHostTable::kSlots + 1));
    PCHECK(t.assign("") == 0 && t.assign("bad host") == 0 && t.assign("a/b") == 0 &&
           t.assign(std::string(128, 'a')) == 0);
    PCHECK(t.assign(std::string(127, 'a')) != 0);
    t.clear();
    PCHECK(t.hostFor(a) == nullptr && t.assign("x.org") == a);  // a cleared table starts over
  }
  {  // the oldest name goes when the table is full
    FakeHostTable t;
    const uint32_t first = t.assign("h0.example");
    for (size_t i = 1; i < FakeHostTable::kSlots; ++i) t.assign("h" + std::to_string(i) + ".example");
    PCHECK(t.assign("h0.example") == first);  // still there: full is not over
    const uint32_t extra = t.assign("new.example");
    PCHECK(extra == first && *t.hostFor(first) == "new.example");
    PCHECK(t.assign("h1.example") == first + 1);
  }
  {  // CONNECT
    PCHECK(connectRequest("a.example", 443) == "CONNECT a.example:443 HTTP/1.1\r\nHost: a.example:443\r\n\r\n");
    PCHECK(reply("HTTP/1.1 200 Connection Established\r\n\r\n") == ConnectReply::Ok);
    PCHECK(reply("HTTP/1.0 200 OK\r\nProxy-Agent: x\r\n\r\n") == ConnectReply::Ok);
    PCHECK(reply("HTTP/1.1 403 destination not allowed\r\nContent-Length: 0\r\n\r\n") == ConnectReply::Refused);
    PCHECK(reply("HTTP/1.1 502 cannot connect\r\n\r\n") == ConnectReply::Refused);
    PCHECK(reply("HTTP/1.1 200 Conn") == ConnectReply::Incomplete);
    PCHECK(reply("") == ConnectReply::Incomplete);
    PCHECK(reply("SSH-2.0-OpenSSH\r\n\r\n") == ConnectReply::Refused);
    PCHECK(reply("HTTP/1.1 2\r\n\r\n") == ConnectReply::Refused);
    const std::string junk(2000, 'x');
    PCHECK(parseConnectReply(junk.data(), junk.size()) == ConnectReply::Refused);
  }
  return failed;
}
