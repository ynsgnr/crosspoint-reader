#include "ota/OtaManifest.h"

#include <algorithm>

#include "ota/MiniJson.h"

namespace mirror::ota {
namespace {

bool isHex(const std::string& s, size_t minLen, size_t maxLen) {
  if (s.size() < minLen || s.size() > maxLen) return false;
  return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return !prefix.empty() && s.size() > prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool str(const json::Value& o, const char* key, std::string& out) {
  const json::Value* v = o.get(key);
  if (!v || v->type != json::Value::Type::String) return false;
  out = v->s;
  return true;
}

bool num(const json::Value& o, const char* key, uint32_t& out) {
  const json::Value* v = o.get(key);
  if (!v || v->type != json::Value::Type::Int || v->i < 0 || v->i > 0xFFFFFFFFLL) return false;
  out = static_cast<uint32_t>(v->i);
  return true;
}

bool readChannel(const json::Value& o, Channel& c) {
  return o.type == json::Value::Type::Object && str(o, "id", c.id) && str(o, "title", c.title) &&
         str(o, "rev", c.rev) && str(o, "board", c.board) && str(o, "url", c.url) && str(o, "sha256", c.sha256) &&
         num(o, "size", c.size) && num(o, "built", c.built);
}

// "pr-12" -> 12; develop sorts before every PR.
long order(const std::string& id) { return id == "develop" ? -1 : std::stol(id.substr(3)); }

}  // namespace

bool isChannelId(const std::string& id) {
  if (id == "develop") return true;
  if (id.size() < 4 || id.size() > 9 || id.compare(0, 3, "pr-") != 0) return false;
  return std::all_of(id.begin() + 3, id.end(), [](char c) { return c >= '0' && c <= '9'; }) && id[3] != '0';
}

bool channelAllowed(const Channel& c, const Policy& p) {
  if (!isChannelId(c.id) || c.board != p.board) return false;
  if (!startsWith(c.url, p.urlPrefix)) return false;
  const std::string name = c.url.substr(p.urlPrefix.size());
  // A file name inside the release, nothing else: no path, query, fragment or escape that could point elsewhere.
  if (name.size() < 5 || name.compare(name.size() - 4, 4, ".bin") != 0) return false;
  if (!std::all_of(name.begin(), name.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
               ch == '_' || ch == '.';
      }))
    return false;
  if (name.find("..") != std::string::npos) return false;
  if (!isHex(c.sha256, 64, 64) || !isHex(c.rev, 7, 12) || c.built == 0) return false;
  if (c.size < p.minImageBytes || (p.maxImageBytes != 0 && c.size > p.maxImageBytes)) return false;
  return !c.title.empty();
}

bool differsFromRunning(const Channel& c, const std::string& runningRev) {
  if (runningRev.empty() || runningRev.back() == '+') return true;
  return c.rev != runningRev;
}

Trust openManifest(const std::string& text, const uint8_t* sig, size_t sigLen, const SignatureVerifier& verifier,
                   const Policy& policy, Manifest& out) {
  out = Manifest{};
  if (text.empty() || text.size() > json::kMaxDocument || !sig || sigLen == 0 ||
      !verifier.verify(reinterpret_cast<const uint8_t*>(text.data()), text.size(), sig, sigLen))
    return Trust::BadSignature;  // nothing below runs on bytes that are not ours

  json::Value doc;
  uint32_t version = 0;
  if (!json::parse(text, doc) || doc.type != json::Value::Type::Object || !num(doc, "v", version) || version != 1 ||
      !num(doc, "issued", out.issued) || out.issued == 0)
    return Trust::BadDocument;
  const json::Value* list = doc.get("channels");
  if (!list || list->type != json::Value::Type::Array) return Trust::BadDocument;
  if (out.issued < policy.lastIssued) return Trust::Stale;

  for (const json::Value& item : list->items) {
    Channel c;
    if (!readChannel(item, c)) return (out = Manifest{}, Trust::BadDocument);
    if (!channelAllowed(c, policy)) continue;
    if (std::any_of(out.channels.begin(), out.channels.end(), [&](const Channel& o) { return o.id == c.id; })) continue;
    out.channels.push_back(std::move(c));
  }
  std::stable_sort(out.channels.begin(), out.channels.end(), [](const Channel& a, const Channel& b) {
    const long x = order(a.id), y = order(b.id);
    return x == -1 || y == -1 ? x < y : x > y;  // develop first, then newest PR first
  });
  if (out.channels.size() > kMaxChannels) out.channels.resize(kMaxChannels);
  return Trust::Ok;
}

const char* trustName(Trust t) {
  switch (t) {
    case Trust::Ok:
      return "OK";
    case Trust::BadSignature:
      return "BAD_SIGNATURE";
    case Trust::BadDocument:
      return "BAD_DOCUMENT";
    case Trust::Stale:
      return "STALE";
  }
  return "?";
}

}  // namespace mirror::ota
