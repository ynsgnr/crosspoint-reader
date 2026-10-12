#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Remembers which OPDS entries auto-sync handled, as 64-bit hashes of their keys.
// Once full the oldest hash is dropped, so a book that scrolled far out of the
// feed may be fetched again.
class OpdsSeenLedger {
 public:
  static constexpr size_t CAPACITY = 512;
  static constexpr size_t MAX_SERIALIZED_BYTES = 8 + CAPACITY * 8;

  static uint64_t hashKey(const std::string& key);

  bool contains(const std::string& key) const;
  void add(const std::string& key);
  size_t size() const { return hashes.size(); }

  void serialize(std::vector<uint8_t>& out) const;
  bool deserialize(const uint8_t* data, size_t len);

 private:
  std::vector<uint64_t> hashes;  // oldest first
};

struct OpdsPagePlan {
  std::vector<size_t> fetch;  // indices into the page's keys, feed order
  bool allSeen = false;       // the page had books and all were seen: nothing further down is new
};

OpdsPagePlan planOpdsPage(const std::vector<std::string>& keys, const OpdsSeenLedger& ledger, size_t limit);
