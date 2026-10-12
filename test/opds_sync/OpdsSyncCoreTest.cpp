#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "OpdsSyncCore.h"

namespace {

using namespace opdssync;

Book makeBook(const std::string& id) { return Book{id, "Title " + id, "Author", "/dl/" + id + ".epub"}; }

Page makePage(const std::vector<std::string>& ids, const std::string& next = "") {
  Page page;
  for (const auto& id : ids) page.books.push_back(makeBook(id));
  page.nextHref = next;
  return page;
}

class FakeBackend final : public Backend {
 public:
  std::map<std::string, Page> pages;        // by resolved page URL
  std::set<std::string> files;              // paths already on the card
  std::set<std::string> failingBooks;       // download fails (Failed) for these hrefs
  std::set<std::string> unauthorizedBooks;  // download fails (Unauthorized) for these hrefs
  bool hasLedger = false;
  OpdsSeenLedger ledger;
  int saveCount = 0;
  int continueBudget = 1000;  // canContinue() calls left before it turns false
  std::vector<std::string> fetched;
  std::vector<std::string> downloadedUrls;
  std::vector<std::string> savedPaths;

  bool canContinue() override { return continueBudget-- > 0; }

  Page fetchPage(const std::string& url) override {
    fetched.push_back(url);
    const auto it = pages.find(url);
    if (it == pages.end()) {
      Page failed;
      failed.status = Page::Status::Failed;
      return failed;
    }
    return it->second;
  }

  std::string resolveUrl(const std::string& base, const std::string& href) override {
    (void)base;
    return "http://srv" + href;  // hrefs here are absolute paths
  }

  std::string destinationPath(const Book& book) override { return "/books/" + book.title + ".epub"; }
  bool fileExists(const std::string& path) override { return files.count(path) > 0; }

  DownloadResult download(const std::string& url, const std::string& destPath) override {
    downloadedUrls.push_back(url);
    for (const auto& href : unauthorizedBooks) {
      if (url == "http://srv" + href) return DownloadResult::Unauthorized;
    }
    for (const auto& href : failingBooks) {
      if (url == "http://srv" + href) return DownloadResult::Failed;
    }
    files.insert(destPath);
    return DownloadResult::Ok;
  }

  bool loadLedger(OpdsSeenLedger& out) override {
    if (!hasLedger) return false;
    out = ledger;
    return true;
  }

  void saveLedger(const OpdsSeenLedger& in) override {
    ledger = in;
    hasLedger = true;
    saveCount++;
  }

  void onBookSaved(const std::string& destPath) override { savedPaths.push_back(destPath); }
};

const std::string FEED = "http://srv/feed";

TEST(OpdsSyncCore, FirstRunOnlyRecordsTheFeed) {
  FakeBackend backend;
  backend.pages[FEED] = makePage({"a", "b"});
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_TRUE(backend.downloadedUrls.empty());
  EXPECT_EQ(summary.downloaded, 0);
  EXPECT_EQ(summary.failed, 0);
  EXPECT_EQ(backend.saveCount, 1);
  EXPECT_TRUE(backend.ledger.contains("a"));
  EXPECT_TRUE(backend.ledger.contains("b"));
}

TEST(OpdsSyncCore, FirstRunBaselinesUpToMaxPages) {
  FakeBackend backend;
  backend.pages[FEED] = makePage({"a"}, "/p2");
  backend.pages["http://srv/p2"] = makePage({"b"}, "/p3");
  backend.pages["http://srv/p3"] = makePage({"c"}, "/p4");
  backend.pages["http://srv/p4"] = makePage({"d"});
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_EQ(backend.fetched.size(), static_cast<size_t>(MAX_PAGES));
  EXPECT_TRUE(backend.ledger.contains("c"));
  EXPECT_FALSE(backend.ledger.contains("d"));
  EXPECT_TRUE(backend.downloadedUrls.empty());
}

TEST(OpdsSyncCore, LaterRunDownloadsOnlyUnseenBooks) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.ledger.add("a");
  backend.pages[FEED] = makePage({"new", "a"});
  Summary summary;
  syncFeed(backend, FEED, summary);

  ASSERT_EQ(backend.downloadedUrls.size(), 1u);
  EXPECT_EQ(backend.downloadedUrls[0], "http://srv/dl/new.epub");
  EXPECT_EQ(summary.downloaded, 1);
  EXPECT_EQ(backend.savedPaths, (std::vector<std::string>{"/books/Title new.epub"}));
  EXPECT_TRUE(backend.ledger.contains("new"));
}

TEST(OpdsSyncCore, SecondRunWithNoNewBooksDownloadsAndSavesNothing) {
  FakeBackend backend;
  backend.pages[FEED] = makePage({"a", "b"});
  Summary first;
  syncFeed(backend, FEED, first);  // baseline
  const int savesAfterBaseline = backend.saveCount;

  Summary second;
  syncFeed(backend, FEED, second);
  EXPECT_TRUE(backend.downloadedUrls.empty());
  EXPECT_EQ(second.downloaded, 0);
  EXPECT_EQ(backend.saveCount, savesAfterBaseline);
}

TEST(OpdsSyncCore, NewBookAppearsBetweenRuns) {
  FakeBackend backend;
  backend.pages[FEED] = makePage({"a"});
  Summary first;
  syncFeed(backend, FEED, first);

  backend.pages[FEED] = makePage({"z", "a"});
  Summary second;
  syncFeed(backend, FEED, second);
  EXPECT_EQ(second.downloaded, 1);
  EXPECT_EQ(backend.downloadedUrls, (std::vector<std::string>{"http://srv/dl/z.epub"}));
}

TEST(OpdsSyncCore, StopsPagingAtFirstFullySeenPage) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.ledger.add("old1");
  backend.ledger.add("old2");
  backend.pages[FEED] = makePage({"old1", "old2"}, "/p2");
  backend.pages["http://srv/p2"] = makePage({"newer"});
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_EQ(backend.fetched, (std::vector<std::string>{FEED}));
  EXPECT_TRUE(backend.downloadedUrls.empty());
}

TEST(OpdsSyncCore, CapsDownloadsPerRunAndKeepsTheRestForNextTime) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.ledger.add("seed");
  std::vector<std::string> ids;
  for (int i = 0; i < MAX_BOOKS_PER_SERVER + 3; i++) ids.push_back("b" + std::to_string(i));
  backend.pages[FEED] = makePage(ids);
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_EQ(summary.downloaded, MAX_BOOKS_PER_SERVER);
  EXPECT_FALSE(backend.ledger.contains("b" + std::to_string(MAX_BOOKS_PER_SERVER)));

  Summary next;
  syncFeed(backend, FEED, next);
  EXPECT_EQ(next.downloaded, 3);
}

TEST(OpdsSyncCore, FailedDownloadIsRetriedNextRun) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.ledger.add("seed");
  backend.pages[FEED] = makePage({"a", "b"});
  backend.failingBooks.insert("/dl/a.epub");
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_EQ(summary.failed, 1);
  EXPECT_EQ(summary.downloaded, 1);
  EXPECT_FALSE(backend.ledger.contains("a"));
  EXPECT_TRUE(backend.ledger.contains("b"));

  backend.failingBooks.clear();
  Summary retry;
  syncFeed(backend, FEED, retry);
  EXPECT_EQ(retry.downloaded, 1);
  EXPECT_TRUE(backend.ledger.contains("a"));
}

TEST(OpdsSyncCore, FeedAuthFailureStopsTheRun) {
  FakeBackend backend;
  backend.hasLedger = true;
  Page denied;
  denied.status = Page::Status::AuthFailed;
  backend.pages[FEED] = denied;
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_TRUE(summary.authFailed);
  EXPECT_EQ(summary.failed, 1);
  EXPECT_TRUE(backend.downloadedUrls.empty());
  EXPECT_EQ(backend.saveCount, 0);
}

TEST(OpdsSyncCore, UnreachableFeedCountsAsFailure) {
  FakeBackend backend;
  Summary summary;
  syncFeed(backend, FEED, summary);
  EXPECT_EQ(summary.failed, 1);
  EXPECT_FALSE(summary.authFailed);
  EXPECT_EQ(backend.saveCount, 0);
}

TEST(OpdsSyncCore, DownloadAuthFailureStopsRemainingBooks) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.ledger.add("seed");
  backend.pages[FEED] = makePage({"a", "b", "c"});
  backend.unauthorizedBooks.insert("/dl/a.epub");
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_TRUE(summary.authFailed);
  EXPECT_EQ(summary.downloaded, 0);
  EXPECT_EQ(backend.downloadedUrls.size(), 1u);
}

TEST(OpdsSyncCore, BookAlreadyOnTheCardIsMarkedSeenNotDownloaded) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.ledger.add("seed");
  backend.pages[FEED] = makePage({"a"});
  backend.files.insert("/books/Title a.epub");
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_TRUE(backend.downloadedUrls.empty());
  EXPECT_EQ(summary.downloaded, 0);
  EXPECT_TRUE(backend.ledger.contains("a"));
}

TEST(OpdsSyncCore, DeadlineBeforeFetchCountsAsFailureAndFetchesNothing) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.continueBudget = 0;
  backend.pages[FEED] = makePage({"a"});
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_EQ(summary.failed, 1);
  EXPECT_TRUE(backend.fetched.empty());
}

TEST(OpdsSyncCore, DeadlineBetweenBooksKeepsEarlierProgress) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.ledger.add("seed");
  backend.continueBudget = 2;  // page fetch + first book only
  backend.pages[FEED] = makePage({"a", "b"});
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_EQ(summary.downloaded, 1);
  EXPECT_EQ(summary.failed, 1);
  EXPECT_TRUE(backend.ledger.contains("a"));
  EXPECT_FALSE(backend.ledger.contains("b"));
  EXPECT_EQ(backend.saveCount, 1);
}

TEST(OpdsSyncCore, FollowsNextPageUntilBudgetOrSeenPage) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.ledger.add("old");
  backend.pages[FEED] = makePage({"a"}, "/p2");
  backend.pages["http://srv/p2"] = makePage({"b", "old"});
  Summary summary;
  syncFeed(backend, FEED, summary);

  EXPECT_EQ(summary.downloaded, 2);
  EXPECT_EQ(backend.fetched.size(), 2u);
}

TEST(OpdsSyncCore, FeedWithoutBooksDownloadsNothing) {
  FakeBackend backend;
  backend.hasLedger = true;
  backend.pages[FEED] = makePage({});
  Summary summary;
  syncFeed(backend, FEED, summary);
  EXPECT_EQ(summary.downloaded, 0);
  EXPECT_EQ(summary.failed, 0);
}

}  // namespace
