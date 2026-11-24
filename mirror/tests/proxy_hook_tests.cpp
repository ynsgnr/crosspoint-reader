// Runs the real device/ProxyHook.cpp against a fake CONNECT proxy on loopback.
#ifndef CROSSPOINT_MIRROR
#define CROSSPOINT_MIRROR 1  // the hook is firmware code: its header is empty without this
#endif
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "device/ProxyHook.h"

extern "C" {
int __wrap_lwip_getaddrinfo(const char*, const char*, const struct addrinfo*, struct addrinfo**);
int __wrap_lwip_connect(int, const struct sockaddr*, socklen_t);
// What the hook calls underneath (in the firmware the linker points these at lwIP).
int __real_lwip_getaddrinfo(const char* n, const char* s, const struct addrinfo* h, struct addrinfo** r) {
  return getaddrinfo(n, s, h, r);
}
int __real_lwip_connect(int fd, const struct sockaddr* a, socklen_t l) { return connect(fd, a, l); }
}

namespace {

#define HCHECK(c)                                              \
  do {                                                         \
    ++checks;                                                  \
    if (!(c)) {                                                \
      ++failed;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                          \
  } while (0)

// A proxy for one connection: reads the head, remembers it, answers with `status`, then echoes what the client sends.
struct FakeProxy {
  int listener = -1;
  uint16_t port = 0;
  std::string status = "HTTP/1.1 200 Connection Established\r\n\r\n";
  std::string seen;
  std::thread worker;

  FakeProxy() {
    listener = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    listen(listener, 4);
    socklen_t len = sizeof(a);
    getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
    port = ntohs(a.sin_port);
  }
  void serveOne() {
    worker = std::thread([this] {
      const int c = accept(listener, nullptr, nullptr);
      if (c < 0) return;
      char buf[512];
      std::string head;
      while (head.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        head.append(buf, static_cast<size_t>(n));
      }
      seen = head;
      send(c, status.data(), status.size(), 0);
      if (status.compare(0, 12, "HTTP/1.1 200") == 0) {
        const ssize_t n = recv(c, buf, sizeof(buf), 0);
        if (n > 0) send(c, buf, static_cast<size_t>(n), 0);
      }
      close(c);
    });
  }
  ~FakeProxy() {
    if (worker.joinable()) worker.join();
    close(listener);
  }
};

int nonBlockingSocket(int type = SOCK_STREAM) {
  const int fd = socket(AF_INET, type, 0);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);  // as WiFiClient does before connect
  return fd;
}

uint32_t lookup(const char* host) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (__wrap_lwip_getaddrinfo(host, "0", &hints, &res) != 0 || !res) return 0;
  const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr);
  freeaddrinfo(res);
  return ip;
}

sockaddr_in addr(uint32_t ip, uint16_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(ip);
  a.sin_port = htons(port);
  return a;
}

// Connect the way the Arduino core does, then check the socket really is usable for a round trip.
bool connectAndEcho(uint32_t ip, uint16_t port) {
  const int fd = nonBlockingSocket();
  const sockaddr_in a = addr(ip, port);
  int res = __wrap_lwip_connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
  bool ok = res == 0;
  if (ok) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd, &w);
    timeval tv{2, 0};
    ok = select(fd + 1, nullptr, &w, nullptr, &tv) == 1;  // writable at once, as the caller expects
  }
  if (ok) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
    timeval rt{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));
    char reply[8] = {};
    ok = send(fd, "ping", 4, 0) == 4 && recv(fd, reply, sizeof(reply), 0) == 4 && std::memcmp(reply, "ping", 4) == 0;
  }
  close(fd);
  return ok;
}

}  // namespace

int runProxyHookTests(int& checks) {
  using namespace mirror::proxyhook;
  int failed = 0;
  const uint32_t loopback = 0x7F000001;

  {  // off: lookups go to the real resolver untouched
    HCHECK(!active());
    HCHECK(lookup("127.0.0.1") == loopback);
  }
  {  // a name becomes a fake address and the connection goes through CONNECT
    FakeProxy proxy;
    proxy.serveOne();
    enable(loopback, proxy.port, 0x0A000000, 0xFF000000);  // the "hotspot" is 10/8; the proxy is on loopback
    HCHECK(active());
    const uint32_t fake = lookup("books.example.org");
    HCHECK(fake == 0xC6120001);
    HCHECK(lookup("books.example.org") == fake);  // stable
    HCHECK(connectAndEcho(fake, 443));
    if (proxy.worker.joinable()) proxy.worker.join();
    HCHECK(proxy.seen == "CONNECT books.example.org:443 HTTP/1.1\r\nHost: books.example.org:443\r\n\r\n");
    disable();
    HCHECK(!active());
  }
  {  // a literal public address is tunnelled too; a literal in the hotspot is not
    FakeProxy proxy;
    proxy.serveOne();
    enable(loopback, proxy.port, 0x0A000000, 0xFF000000);
    HCHECK(lookup("93.184.216.34") == 0x5DB8D822);  // literals are not renamed
    HCHECK(connectAndEcho(0x5DB8D822, 80));
    if (proxy.worker.joinable()) proxy.worker.join();
    HCHECK(proxy.seen.rfind("CONNECT 93.184.216.34:80 HTTP/1.1", 0) == 0);
    disable();
  }
  {  // the CONNECT exchange must not leave its 10 s step timeout on the socket (a later pause over 10 s would end the
     // transfer)
    FakeProxy proxy;
    proxy.serveOne();
    enable(loopback, proxy.port, 0x0A000000, 0xFF000000);
    const int fd = nonBlockingSocket();
    timeval mine{37, 0};  // what the caller had set before connecting
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &mine, sizeof(mine));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &mine, sizeof(mine));
    const sockaddr_in a = addr(lookup("slow.example"), 443);
    HCHECK(__wrap_lwip_connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0);
    timeval rcv{}, snd{};
    socklen_t len = sizeof(rcv);
    getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, &len);
    len = sizeof(snd);
    getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, &len);
    HCHECK(rcv.tv_sec == 37 && snd.tv_sec == 37);
    HCHECK((fcntl(fd, F_GETFL, 0) & O_NONBLOCK) != 0);  // and it is non-blocking again, as the caller left it
    close(fd);
    if (proxy.worker.joinable()) proxy.worker.join();
    disable();
  }
  {  // a refusal fails the connect with an errno the caller treats as an error (never EINPROGRESS)
    FakeProxy proxy;
    proxy.status = "HTTP/1.1 403 destination not allowed\r\nContent-Length: 0\r\n\r\n";
    proxy.serveOne();
    enable(loopback, proxy.port, 0x0A000000, 0xFF000000);
    const int fd = nonBlockingSocket();
    const sockaddr_in a = addr(lookup("blocked.example"), 443);
    errno = 0;
    HCHECK(__wrap_lwip_connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == -1);
    HCHECK(errno != EINPROGRESS && errno != 0);
    close(fd);
    if (proxy.worker.joinable()) proxy.worker.join();
    disable();
  }
  {  // no proxy listening: the connect fails, again not with EINPROGRESS
    FakeProxy proxy;
    const uint16_t dead = proxy.port;
    close(proxy.listener);
    proxy.listener = socket(AF_INET, SOCK_STREAM, 0);  // keeps the destructor's close() valid
    enable(loopback, dead, 0x0A000000, 0xFF000000);
    const int fd = nonBlockingSocket();
    const sockaddr_in a = addr(lookup("nobody.example"), 443);
    errno = 0;
    HCHECK(__wrap_lwip_connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == -1 && errno != EINPROGRESS);
    close(fd);
    disable();
  }
  {  // not everything is a candidate: IPv6 asks, UDP sockets, the hotspot's own addresses, and the proxy itself
    FakeProxy proxy;
    enable(loopback, proxy.port, 0x7F000000, 0xFF000000);  // here the "hotspot" covers loopback
    addrinfo hints{};
    hints.ai_family = AF_INET6;
    addrinfo* res = nullptr;
    HCHECK(__wrap_lwip_getaddrinfo("books.example.org", "0", &hints, &res) == EAI_FAMILY);
    const int udp = nonBlockingSocket(SOCK_DGRAM);
    const sockaddr_in fakeDest = addr(0xC6120001, 53);
    HCHECK(__wrap_lwip_connect(udp, reinterpret_cast<const sockaddr*>(&fakeDest), sizeof(fakeDest)) ==
           0);  // UDP goes to the real connect
    close(udp);
    proxy.serveOne();
    const int tcp = nonBlockingSocket();
    const sockaddr_in direct = addr(loopback, proxy.port);
    const int r = __wrap_lwip_connect(tcp, reinterpret_cast<const sockaddr*>(&direct), sizeof(direct));
    HCHECK(r == 0 || (r == -1 && errno == EINPROGRESS));  // the real, untouched, non-blocking connect
    close(tcp);
    if (proxy.worker.joinable()) proxy.worker.join();
    disable();
    proxy.seen.clear();
  }
  {  // a fake address whose name is unknown (table cleared) is unreachable, not a connection to nowhere
    FakeProxy proxy;
    enable(loopback, proxy.port, 0x0A000000, 0xFF000000);
    const int fd = nonBlockingSocket();
    const sockaddr_in a = addr(0xC6120005, 443);
    HCHECK(__wrap_lwip_connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == -1 && errno == EHOSTUNREACH);
    close(fd);
    disable();
  }
  return failed;
}
