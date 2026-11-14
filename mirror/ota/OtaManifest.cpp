#include "ota/OtaManifest.h"

#include <algorithm>

namespace mirror::ota {
namespace {

bool isHex(const std::string& s, size_t minLen, size_t maxLen) {
  return s.size() >= minLen && s.size() <= maxLen &&
         std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool isNumber(const std::string& s, uint32_t& out) {
  if (s.empty() || s.size() > 10 || !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }))
    return false;
  const unsigned long long v = std::stoull(s);
  if (v == 0 || v > 0xFFFFFFFFull) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

// Takes the next space-separated word of `line` starting at `pos`.
bool word(const std::string& line, size_t& pos, std::string& out) {
  const size_t end = line.find(' ', pos);
  if (end == std::string::npos || end == pos) return false;
  out = line.substr(pos, end - pos);
  pos = end + 1;
  return true;
}

bool readChannel(const std::string& line, Channel& c) {
  size_t pos = 0;
  std::string built, size;
  if (!word(line, pos, c.id) || !word(line, pos, c.rev) || !word(line, pos, built) || !word(line, pos, size) ||
      !word(line, pos, c.sha256))
    return false;
  c.title = line.substr(pos);
  // A title is printable ASCII or UTF-8 text on one line.
  const bool titleOk = !c.title.empty() && c.title.size() <= 96 && c.title.front() != ' ' &&
                       std::all_of(c.title.begin(), c.title.end(),
                                   [](char ch) { return static_cast<unsigned char>(ch) >= 0x20 && ch != 0x7F; });
  return titleOk && isChannelId(c.id) && isHex(c.rev, 7, 12) && isHex(c.sha256, 64, 64) && isNumber(built, c.built) &&
         isNumber(size, c.size) && c.size >= 512 * 1024;
}

}  // namespace

bool isChannelId(const std::string& id) {
  if (id == "develop") return true;
  if (id.size() < 4 || id.size() > 9 || id.compare(0, 3, "pr-") != 0 || id[3] == '0') return false;
  return std::all_of(id.begin() + 3, id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool differsFromRunning(const Channel& c, const std::string& runningRev) {
  return runningRev.empty() || runningRev.back() == '+' || c.rev != runningRev;
}

Trust openManifest(const std::string& text, const uint8_t* sig, size_t sigLen, const SignatureVerifier& verifier,
                   std::vector<Channel>& out) {
  out.clear();
  if (text.empty() || text.size() > kMaxManifestBytes || !sig || sigLen == 0 ||
      !verifier.verify(reinterpret_cast<const uint8_t*>(text.data()), text.size(), sig, sigLen))
    return Trust::BadSignature;  // nothing below runs on bytes that are not ours

  std::vector<Channel> channels;
  for (size_t pos = 0; pos < text.size();) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) return Trust::BadFormat;  // every line, the last one too, ends in a newline
    Channel c;
    if (channels.size() == kMaxChannels || !readChannel(text.substr(pos, end - pos), c)) return Trust::BadFormat;
    if (std::any_of(channels.begin(), channels.end(), [&](const Channel& o) { return o.id == c.id; }))
      return Trust::BadFormat;
    channels.push_back(std::move(c));
    pos = end + 1;
  }
  out = std::move(channels);
  return Trust::Ok;
}

}  // namespace mirror::ota
