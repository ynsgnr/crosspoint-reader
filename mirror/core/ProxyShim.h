#pragma once
// The pure part of the phone proxy shim (device/ProxyHook.cpp): fake addresses for host names and the CONNECT exchange.
// While a phone sync is on, every TCP connection leaves through the phone's proxy: a lookup gives a name a fake
// address, and connecting to a fake address sends "CONNECT name:port" to the proxy instead.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mirror {

class FakeHostTable {
 public:
  static constexpr uint32_t kBase = 0xC6120000;  // 198.18.0.0 (the benchmarking range: never a real destination)
  static constexpr size_t kSlots = 32;
  static constexpr size_t kMaxHost = 127;

  // A stable fake address (host byte order) for `host`: the same name gets the same address while it is in the table,
  // so a redirect or a second request reuses it. 0 when the name is empty, too long or not a plain host name. When full
  // the oldest name is replaced; an address handed out for it earlier then names the new host (32 names is far more
  // than a sync uses).
  uint32_t assign(const std::string& host);
  static bool isFake(uint32_t ip) { return ip > kBase && ip <= kBase + kSlots; }
  // The name behind a fake address, or nullptr.
  const std::string* hostFor(uint32_t ip) const;
  void clear();

 private:
  std::array<std::string, kSlots> names_;
  size_t next_ = 0;  // the slot to fill next (oldest first)
};

bool isPlainHostName(const std::string& host);

// "CONNECT host:port HTTP/1.1" with a Host header and the blank line.
std::string connectRequest(const std::string& host, uint16_t port);

enum class ConnectReply { Incomplete, Ok, Refused };
// Reads the proxy's answer: Incomplete until the blank line has arrived, Ok for 2xx, Refused for anything else or a
// head over 1 KB.
ConnectReply parseConnectReply(const char* data, size_t len);

}  // namespace mirror
