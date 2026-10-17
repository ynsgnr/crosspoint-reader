#include <gtest/gtest.h>

#include <cstring>

#include "ImageFingerprint.h"

namespace {

uint32_t hashOf(const char* text) { return fnv1a32(reinterpret_cast<const uint8_t*>(text), std::strlen(text)); }

TEST(ImageFingerprint, MatchesPublishedFnv1aVectors) {
  EXPECT_EQ(hashOf(""), 0x811C9DC5u);
  EXPECT_EQ(hashOf("a"), 0xE40C292Cu);
  EXPECT_EQ(hashOf("foobar"), 0xBF9CF968u);
}

TEST(ImageFingerprint, ChunkedHashEqualsOneShotHash) {
  const char* text = "a 48 KB bitmap read in 512 byte chunks";
  const auto* bytes = reinterpret_cast<const uint8_t*>(text);
  const size_t length = std::strlen(text);
  for (size_t split = 0; split <= length; split++) {
    const uint32_t first = fnv1a32(bytes, split);
    EXPECT_EQ(fnv1a32(bytes + split, length - split, first), hashOf(text)) << "split at " << split;
  }
}

TEST(ImageFingerprint, DifferentContentGivesDifferentHash) {
  EXPECT_NE(hashOf("screen one"), hashOf("screen two"));
  EXPECT_NE(hashOf("ab"), hashOf("ba"));
}

}  // namespace
