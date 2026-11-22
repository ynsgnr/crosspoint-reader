#include "core/ProxyShim.h"

#include <algorithm>
#include <cstring>

namespace mirror {

bool isPlainHostName(const std::string& host) {
  if (host.empty() || host.size() > FakeHostTable::kMaxHost) return false;
  return std::all_of(host.begin(), host.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
           c == '_';
  });
}

uint32_t FakeHostTable::assign(const std::string& host) {
  if (!isPlainHostName(host)) return 0;
  for (size_t i = 0; i < kSlots; ++i)
    if (names_[i] == host) return kBase + 1 + static_cast<uint32_t>(i);
  const size_t slot = next_;
  names_[slot] = host;
  next_ = (next_ + 1) % kSlots;
  return kBase + 1 + static_cast<uint32_t>(slot);
}

const std::string* FakeHostTable::hostFor(uint32_t ip) const {
  if (!isFake(ip)) return nullptr;
  const std::string& name = names_[ip - kBase - 1];
  return name.empty() ? nullptr : &name;
}

void FakeHostTable::clear() {
  for (auto& n : names_) n.clear();
  next_ = 0;
}

std::string connectRequest(const std::string& host, uint16_t port) {
  const std::string target = host + ":" + std::to_string(port);
  return "CONNECT " + target + " HTTP/1.1\r\nHost: " + target + "\r\n\r\n";
}

ConnectReply parseConnectReply(const char* data, size_t len) {
  constexpr size_t kMaxHead = 1024;
  const std::string head(data, std::min(len, kMaxHead + 4));
  const size_t end = head.find("\r\n\r\n");
  if (end == std::string::npos) return len > kMaxHead ? ConnectReply::Refused : ConnectReply::Incomplete;
  if (head.compare(0, 5, "HTTP/") != 0) return ConnectReply::Refused;
  const size_t space = head.find(' ');
  if (space == std::string::npos || space + 3 >= head.size()) return ConnectReply::Refused;
  const bool ok = head[space + 1] == '2' && head[space + 2] >= '0' && head[space + 2] <= '9' &&
                  head[space + 3] >= '0' && head[space + 3] <= '9';
  return ok ? ConnectReply::Ok : ConnectReply::Refused;
}

}  // namespace mirror
