#if defined(CROSSPOINT_MIRROR)
#include "device/ProxyHook.h"

#include <Logging.h>
#include <fcntl.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <string>

#include "core/ProxyShim.h"

extern "C" {
int __real_lwip_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res);
int __real_lwip_connect(int s, const struct sockaddr* name, socklen_t namelen);
}

namespace {

constexpr int kStepSeconds = 10;  // timeout for each step of the CONNECT exchange
struct State {
  std::atomic<bool> active{false};
  uint32_t proxyIp = 0, subnet = 0, mask = 0;  // host byte order; set before `active`
  uint16_t proxyPort = 0;
  std::mutex mutex;  // the table: lookups come from several tasks
  mirror::FakeHostTable table;
};
State g;

bool isStream(int fd) {
  int type = 0;
  socklen_t len = sizeof(type);
  return lwip_getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0 && type == SOCK_STREAM;
}

// Connects `fd` to the proxy and asks it for host:port. The socket is temporarily made blocking
// for the CONNECT exchange, then returned in its original mode with its original socket timeouts.
bool tunnel(int fd, const std::string& host, uint16_t port) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;

  // Save both options: leaving the temporary timeout installed also limits the subsequent TLS
  // session, which can fail otherwise-valid downloads whenever a transfer pauses for >10 seconds.
  timeval oldSendTimeout{};
  timeval oldRecvTimeout{};
  socklen_t oldSendTimeoutLen = sizeof(oldSendTimeout);
  socklen_t oldRecvTimeoutLen = sizeof(oldRecvTimeout);
  const bool haveOldSendTimeout =
      lwip_getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &oldSendTimeout, &oldSendTimeoutLen) == 0;
  const bool haveOldRecvTimeout =
      lwip_getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &oldRecvTimeout, &oldRecvTimeoutLen) == 0;

  if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) return false;

  const timeval stepTimeout{kStepSeconds, 0};
  bool ok = lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stepTimeout, sizeof(stepTimeout)) == 0 &&
            lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &stepTimeout, sizeof(stepTimeout)) == 0;

  sockaddr_in proxy{};
  proxy.sin_family = AF_INET;
  proxy.sin_port = htons(g.proxyPort);
  proxy.sin_addr.s_addr = htonl(g.proxyIp);

  if (ok) {
    ok = __real_lwip_connect(fd, reinterpret_cast<const sockaddr*>(&proxy), sizeof(proxy)) == 0;
  }
  if (ok) {
    const std::string request = mirror::connectRequest(host, port);
    size_t sent = 0;
    while (ok && sent < request.size()) {
      const int n = lwip_send(fd, request.data() + sent, request.size() - sent, 0);
      if (n <= 0)
        ok = false;
      else
        sent += static_cast<size_t>(n);
    }
  }
  if (ok) {
    char reply[1100];
    size_t have = 0;
    mirror::ConnectReply state = mirror::ConnectReply::Incomplete;
    while (state == mirror::ConnectReply::Incomplete && have < sizeof(reply)) {
      const int n = lwip_recv(fd, reply + have, sizeof(reply) - have, 0);
      if (n <= 0) break;
      have += static_cast<size_t>(n);
      state = mirror::parseConnectReply(reply, have);
    }
    ok = state == mirror::ConnectReply::Ok;
    if (!ok) {
      LOG_ERR("PROXY", "%s:%u refused by the proxy (state %d)", host.c_str(), static_cast<unsigned>(port),
              static_cast<int>(state));
    }
  }

  // Save errno before cleanup: getsockopt/setsockopt/fcntl may overwrite it.
  const int err = errno;
  if (haveOldSendTimeout) {
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &oldSendTimeout, oldSendTimeoutLen);
  }
  if (haveOldRecvTimeout) {
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &oldRecvTimeout, oldRecvTimeoutLen);
  }
  fcntl(fd, F_SETFL, flags);

  if (!ok) {
    errno = (err == 0 || err == EINPROGRESS) ? ECONNREFUSED : err;
  }
  return ok;
}

}  // namespace

namespace mirror::proxyhook {

void enable(uint32_t proxyIp, uint16_t proxyPort, uint32_t subnet, uint32_t subnetMask) {
  disable();
  g.proxyIp = proxyIp;
  g.proxyPort = proxyPort;
  // A zero mask would call every address local: with no usable subnet only the proxy itself stays outside the tunnel.
  g.mask = subnetMask ? subnetMask : 0xFFFFFFFF;
  g.subnet = (subnetMask ? subnet : proxyIp) & g.mask;
  g.active = true;
  LOG_INF("PROXY", "all connections now go through the phone proxy");
}

void disable() {
  if (!g.active.exchange(false)) return;
  std::lock_guard<std::mutex> lock(g.mutex);
  g.table.clear();
  LOG_INF("PROXY", "phone proxy off");
}

bool active() { return g.active; }

}  // namespace mirror::proxyhook

// Names get a fake address while the proxy is on; everything else (literal addresses, IPv6 asks) is left alone.
extern "C" int __wrap_lwip_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                                       struct addrinfo** res) {
  if (!g.active || !node) return __real_lwip_getaddrinfo(node, service, hints, res);
  if (hints && hints->ai_family == AF_INET6)
    return EAI_FAMILY;  // no IPv6 through the proxy; the caller asks again for any family
  in_addr literal;
  if (inet_aton(node, &literal)) return __real_lwip_getaddrinfo(node, service, hints, res);
  uint32_t fake;
  {
    std::lock_guard<std::mutex> lock(g.mutex);
    fake = g.table.assign(node);
  }
  if (fake == 0) return __real_lwip_getaddrinfo(node, service, hints, res);
  char dotted[16];
  snprintf(dotted, sizeof(dotted), "%u.%u.%u.%u", static_cast<unsigned>(fake >> 24),
           static_cast<unsigned>((fake >> 16) & 0xFF), static_cast<unsigned>((fake >> 8) & 0xFF),
           static_cast<unsigned>(fake & 0xFF));
  return __real_lwip_getaddrinfo(dotted, service, hints, res);  // the real function builds a correctly allocated result
}

// A TCP connection to a fake address or to anything outside the hotspot goes to the proxy as a CONNECT.
extern "C" int __wrap_lwip_connect(int s, const struct sockaddr* name, socklen_t namelen) {
  if (!g.active || !name || name->sa_family != AF_INET || namelen < sizeof(sockaddr_in) || !isStream(s))
    return __real_lwip_connect(s, name, namelen);
  const auto* in = reinterpret_cast<const sockaddr_in*>(name);
  const uint32_t dest = ntohl(in->sin_addr.s_addr);
  if (dest == g.proxyIp || (dest & g.mask) == g.subnet)
    return __real_lwip_connect(s, name, namelen);  // the proxy itself, the hotspot
  std::string host;
  if (mirror::FakeHostTable::isFake(dest)) {
    std::lock_guard<std::mutex> lock(g.mutex);
    const std::string* known = g.table.hostFor(dest);
    if (!known) {
      errno = EHOSTUNREACH;
      return -1;
    }
    host = *known;
  } else {  // a literal address
    char dotted[16];
    snprintf(dotted, sizeof(dotted), "%u.%u.%u.%u", static_cast<unsigned>(dest >> 24),
             static_cast<unsigned>((dest >> 16) & 0xFF), static_cast<unsigned>((dest >> 8) & 0xFF),
             static_cast<unsigned>(dest & 0xFF));
    host = dotted;
  }
  return tunnel(s, host, ntohs(in->sin_port)) ? 0 : -1;
}
#endif
