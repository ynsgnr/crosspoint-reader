#include <gtest/gtest.h>

#include "UrlUtils.h"

namespace {

const std::string SERVER = "https://cloud.example/apps/books/opds";

TEST(UrlUtilsBuildUrl, FullUrlIsUsedAsIs) {
  EXPECT_EQ(UrlUtils::buildUrl(SERVER, "https://other.example/feed?x=1"), "https://other.example/feed?x=1");
}

TEST(UrlUtilsBuildUrl, AbsolutePathReplacesTheServerPath) {
  EXPECT_EQ(UrlUtils::buildUrl(SERVER, "/apps/books/opds?sort=recent&per_page=20"),
            "https://cloud.example/apps/books/opds?sort=recent&per_page=20");
}

TEST(UrlUtilsBuildUrl, RelativePathIsAppendedWithOneSlash) {
  EXPECT_EQ(UrlUtils::buildUrl(SERVER, "authors"), "https://cloud.example/apps/books/opds/authors");
  EXPECT_EQ(UrlUtils::buildUrl(SERVER + "/", "authors"), "https://cloud.example/apps/books/opds/authors");
}

TEST(UrlUtilsBuildUrl, QueryOnlyPathAttachesWithoutASlash) {
  EXPECT_EQ(UrlUtils::buildUrl(SERVER, "?sort=recent&per_page=20"),
            "https://cloud.example/apps/books/opds?sort=recent&per_page=20");
}

TEST(UrlUtilsBuildUrl, QueryOnlyPathReplacesTheBaseQuery) {
  EXPECT_EQ(UrlUtils::buildUrl(SERVER + "?sort=title&page=2", "?sort=recent"),
            "https://cloud.example/apps/books/opds?sort=recent");
}

TEST(UrlUtilsBuildUrl, RelativePathDropsTheBaseQuery) {
  EXPECT_EQ(UrlUtils::buildUrl(SERVER + "?sort=title", "authors"), "https://cloud.example/apps/books/opds/authors");
}

TEST(UrlUtilsBuildUrl, EmptyPathReturnsTheServerUrl) { EXPECT_EQ(UrlUtils::buildUrl(SERVER, ""), SERVER); }

TEST(UrlUtilsBuildUrl, ServerWithoutProtocolGetsHttp) {
  EXPECT_EQ(UrlUtils::buildUrl("books.local/opds", "?sort=recent"), "http://books.local/opds?sort=recent");
}

TEST(UrlUtilsBuildUrl, UnsafeCharactersAreEncoded) {
  EXPECT_EQ(UrlUtils::buildUrl(SERVER, "/search?q=a b"), "https://cloud.example/search?q=a%20b");
}

}  // namespace
