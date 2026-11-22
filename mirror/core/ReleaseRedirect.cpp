#include "core/ReleaseRedirect.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace mirror {
namespace {
constexpr size_t kMaxUrl = 2048;

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
}  // namespace

bool splitHttpsUrl(const std::string& url, std::string& host, std::string& path) {
  constexpr char kScheme[] = "https://";
  if (url.size() > kMaxUrl || url.compare(0, sizeof(kScheme) - 1, kScheme) != 0) return false;
  const size_t slash = url.find('/', sizeof(kScheme) - 1);
  if (slash == std::string::npos) return false;
  const std::string h = url.substr(sizeof(kScheme) - 1, slash - (sizeof(kScheme) - 1));
  if (h.empty() || !std::all_of(h.begin(), h.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
      }))
    return false;
  if (std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= ' ' || c == 0x7F; }))
    return false;  // no spaces or control bytes
  host = h;
  path = url.substr(slash);
  return true;
}

std::string releaseRequest(const std::string& host, const std::string& path) {
  return "GET " + path + " HTTP/1.1\r\nHost: " + host +
         "\r\nUser-Agent: CrossPoint-ESP32-ota\r\nAccept: */*\r\nConnection: close\r\n\r\n";
}

bool headComplete(const std::string& head) { return head.find("\r\n\r\n") != std::string::npos; }

RedirectHead parseRedirectHead(const std::string& head) {
  RedirectHead out;
  if (head.compare(0, 5, "HTTP/") != 0) return out;
  const size_t space = head.find(' ');
  if (space == std::string::npos) return out;
  out.status = std::atoi(head.c_str() + space + 1);
  size_t at = head.find("\r\n");
  while (at != std::string::npos && at + 2 < head.size()) {
    const size_t start = at + 2;
    const size_t end = head.find("\r\n", start);
    const std::string line = head.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (line.empty()) break;
    const size_t colon = line.find(':');
    if (colon != std::string::npos && lower(line.substr(0, colon)) == "location") {
      size_t v = colon + 1;
      while (v < line.size() && line[v] == ' ') ++v;
      out.location = line.substr(v);
    }
    at = end;
  }
  return out;
}

bool isGithubAssetUrl(const std::string& url) {
  std::string host, path;
  if (!splitHttpsUrl(url, host, path)) return false;
  constexpr char kSuffix[] = ".githubusercontent.com";
  const std::string h = lower(host);
  return h.size() > sizeof(kSuffix) - 1 && h.compare(h.size() - (sizeof(kSuffix) - 1), std::string::npos, kSuffix) == 0;
}

}  // namespace mirror
