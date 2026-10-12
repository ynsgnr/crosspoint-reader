#include "OpdsSeenLedger.h"

#include <algorithm>

namespace {
constexpr uint32_t LEDGER_MAGIC = 0x314C534F;  // "OSL1"

void putU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

uint64_t getLE(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; i++) v |= static_cast<uint64_t>(p[i]) << (8 * i);
  return v;
}
}  // namespace

uint64_t OpdsSeenLedger::hashKey(const std::string& key) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a 64
  for (const unsigned char c : key) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

bool OpdsSeenLedger::contains(const std::string& key) const {
  return std::find(hashes.begin(), hashes.end(), hashKey(key)) != hashes.end();
}

void OpdsSeenLedger::add(const std::string& key) {
  if (contains(key)) return;
  if (hashes.size() >= CAPACITY) hashes.erase(hashes.begin());
  hashes.push_back(hashKey(key));
}

void OpdsSeenLedger::serialize(std::vector<uint8_t>& out) const {
  out.clear();
  out.reserve(8 + hashes.size() * 8);
  putU32(out, LEDGER_MAGIC);
  putU32(out, static_cast<uint32_t>(hashes.size()));
  for (const uint64_t h : hashes) {
    for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(h >> (8 * i)));
  }
}

bool OpdsSeenLedger::deserialize(const uint8_t* data, const size_t len) {
  hashes.clear();
  if (len < 8 || getLE(data, 4) != LEDGER_MAGIC) return false;
  const uint64_t count = getLE(data + 4, 4);
  if (count > CAPACITY || len < 8 + count * 8) return false;
  hashes.reserve(static_cast<size_t>(count));
  for (uint64_t i = 0; i < count; i++) hashes.push_back(getLE(data + 8 + i * 8, 8));
  return true;
}

OpdsPagePlan planOpdsPage(const std::vector<std::string>& keys, const OpdsSeenLedger& ledger, const size_t limit) {
  OpdsPagePlan plan;
  for (size_t i = 0; i < keys.size(); i++) {
    if (ledger.contains(keys[i])) continue;
    if (plan.fetch.size() < limit) plan.fetch.push_back(i);
  }
  // Counted over the whole page, not just the capped selection: a page with
  // more unseen books than the limit still has unseen books.
  plan.allSeen =
      !keys.empty() && std::all_of(keys.begin(), keys.end(), [&](const std::string& k) { return ledger.contains(k); });
  return plan;
}
