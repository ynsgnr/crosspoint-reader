#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "OpdsSeenLedger.h"

namespace {

TEST(OpdsSeenLedger, ContainsOnlyAddedKeys) {
  OpdsSeenLedger ledger;
  EXPECT_FALSE(ledger.contains("urn:uuid:book-1"));
  ledger.add("urn:uuid:book-1");
  EXPECT_TRUE(ledger.contains("urn:uuid:book-1"));
  EXPECT_FALSE(ledger.contains("urn:uuid:book-2"));
}

TEST(OpdsSeenLedger, AddingTwiceKeepsOneEntry) {
  OpdsSeenLedger ledger;
  ledger.add("a");
  ledger.add("a");
  EXPECT_EQ(ledger.size(), 1u);
}

TEST(OpdsSeenLedger, EvictsOldestWhenFull) {
  OpdsSeenLedger ledger;
  for (size_t i = 0; i < OpdsSeenLedger::CAPACITY + 3; i++) ledger.add("k" + std::to_string(i));
  EXPECT_EQ(ledger.size(), OpdsSeenLedger::CAPACITY);
  EXPECT_FALSE(ledger.contains("k0"));
  EXPECT_FALSE(ledger.contains("k2"));
  EXPECT_TRUE(ledger.contains("k3"));
  EXPECT_TRUE(ledger.contains("k" + std::to_string(OpdsSeenLedger::CAPACITY + 2)));
}

TEST(OpdsSeenLedger, SerializeRoundTrip) {
  OpdsSeenLedger a;
  a.add("one");
  a.add("two");
  std::vector<uint8_t> bytes;
  a.serialize(bytes);

  OpdsSeenLedger b;
  ASSERT_TRUE(b.deserialize(bytes.data(), bytes.size()));
  EXPECT_EQ(b.size(), 2u);
  EXPECT_TRUE(b.contains("one"));
  EXPECT_TRUE(b.contains("two"));
  EXPECT_FALSE(b.contains("three"));
}

TEST(OpdsSeenLedger, FullLedgerFitsSerializedCap) {
  OpdsSeenLedger a;
  for (size_t i = 0; i < OpdsSeenLedger::CAPACITY; i++) a.add("k" + std::to_string(i));
  std::vector<uint8_t> bytes;
  a.serialize(bytes);
  EXPECT_EQ(bytes.size(), OpdsSeenLedger::MAX_SERIALIZED_BYTES);
}

TEST(OpdsSeenLedger, RejectsBadMagicAndTruncation) {
  OpdsSeenLedger a;
  a.add("one");
  std::vector<uint8_t> bytes;
  a.serialize(bytes);

  OpdsSeenLedger b;
  b.add("stale");
  std::vector<uint8_t> bad = bytes;
  bad[0] ^= 0xFF;
  EXPECT_FALSE(b.deserialize(bad.data(), bad.size()));
  EXPECT_EQ(b.size(), 0u);

  EXPECT_FALSE(b.deserialize(bytes.data(), bytes.size() - 1));
  EXPECT_EQ(b.size(), 0u);
  EXPECT_FALSE(b.deserialize(bytes.data(), 3));
}

TEST(OpdsSeenLedger, RejectsOversizedCount) {
  std::vector<uint8_t> bytes = {'O', 'S', 'L', '1', 0xFF, 0xFF, 0x00, 0x00};
  OpdsSeenLedger b;
  EXPECT_FALSE(b.deserialize(bytes.data(), bytes.size()));
}

TEST(PlanOpdsPage, PicksUnseenInFeedOrder) {
  OpdsSeenLedger ledger;
  ledger.add("b");
  const auto plan = planOpdsPage({"a", "b", "c"}, ledger, 10);
  EXPECT_EQ(plan.fetch, (std::vector<size_t>{0, 2}));
  EXPECT_FALSE(plan.allSeen);
}

TEST(PlanOpdsPage, HonorsLimit) {
  OpdsSeenLedger ledger;
  const auto plan = planOpdsPage({"a", "b", "c"}, ledger, 2);
  EXPECT_EQ(plan.fetch, (std::vector<size_t>{0, 1}));
  EXPECT_FALSE(plan.allSeen);
}

TEST(PlanOpdsPage, AllSeenStopsPaging) {
  OpdsSeenLedger ledger;
  ledger.add("a");
  ledger.add("b");
  const auto plan = planOpdsPage({"a", "b"}, ledger, 5);
  EXPECT_TRUE(plan.fetch.empty());
  EXPECT_TRUE(plan.allSeen);
}

TEST(PlanOpdsPage, EmptyPageIsNotAllSeen) {
  OpdsSeenLedger ledger;
  const auto plan = planOpdsPage({}, ledger, 5);
  EXPECT_TRUE(plan.fetch.empty());
  EXPECT_FALSE(plan.allSeen);
}

TEST(PlanOpdsPage, SyncTwiceDownloadsNothingSecondTime) {
  OpdsSeenLedger ledger;
  const std::vector<std::string> feed = {"x", "y"};
  for (const size_t i : planOpdsPage(feed, ledger, 5).fetch) ledger.add(feed[i]);
  EXPECT_TRUE(planOpdsPage(feed, ledger, 5).fetch.empty());
  const std::vector<std::string> grown = {"z", "x", "y"};
  EXPECT_EQ(planOpdsPage(grown, ledger, 5).fetch, (std::vector<size_t>{0}));
}

}  // namespace
