#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * Remembers which OPDS entries auto-sync has already handled, as 64-bit hashes
 * of the entry id (or its download href when the feed gives no id). Bounded:
 * once full, the oldest hash is dropped, so a book that scrolled far out of the
 * feed may be fetched again. 512 hashes serialize to ~4KB.
 */
class OpdsSeenLedger {
 public:
  static constexpr size_t CAPACITY = 512;
  // Serialized size cap, header + CAPACITY hashes. Callers bound file reads with it.
  static constexpr size_t MAX_SERIALIZED_BYTES = 8 + CAPACITY * 8;

  static uint64_t hashKey(const std::string& key);

  bool contains(const std::string& key) const;
  void add(const std::string& key);
  size_t size() const { return hashes.size(); }

  void serialize(std::vector<uint8_t>& out) const;
  // False for a bad magic or a truncated body; the ledger is left empty then.
  bool deserialize(const uint8_t* data, size_t len);

 private:
  std::vector<uint64_t> hashes;  // oldest first
};

struct OpdsPagePlan {
  std::vector<size_t> fetch;  // indices into the page's keys, feed order
  // The page held books and every one was already seen: the feed is newest
  // first, so nothing further down can be new and paging can stop.
  bool allSeen = false;
};

/**
 * Picks the unseen entries of one feed page. `keys` are the book entries'
 * ledger keys in feed order; at most `limit` indices are returned.
 */
OpdsPagePlan planOpdsPage(const std::vector<std::string>& keys, const OpdsSeenLedger& ledger, size_t limit);
