#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "OpdsSeenLedger.h"

namespace opdssync {

constexpr uint8_t MAX_BOOKS_PER_SERVER = 5;
constexpr uint8_t MAX_PAGES = 3;

struct Summary {
  uint8_t servers = 0;
  uint8_t downloaded = 0;
  uint8_t failed = 0;
  bool authFailed = false;
  bool wifiFailed = false;
};

struct Book {
  std::string key;  // entry id, or its download link when the entry has none
  std::string title;
  std::string author;
  std::string href;
};

struct Page {
  enum class Status { Ok, Failed, AuthFailed };
  Status status = Status::Ok;
  std::vector<Book> books;
  std::string nextHref;  // empty on the last page
};

enum class DownloadResult { Ok, Failed, Unauthorized };

// What the sync loop needs from the device, so the loop itself runs in host tests.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool canContinue() = 0;  // false after the deadline or when heap is too low for TLS
  virtual Page fetchPage(const std::string& url) = 0;
  virtual std::string resolveUrl(const std::string& base, const std::string& href) = 0;
  virtual std::string destinationPath(const Book& book) = 0;
  virtual bool fileExists(const std::string& path) = 0;
  virtual DownloadResult download(const std::string& url, const std::string& destPath) = 0;
  virtual bool loadLedger(OpdsSeenLedger& ledger) = 0;
  virtual void saveLedger(const OpdsSeenLedger& ledger) = 0;
  virtual void onBookSaved(const std::string& destPath) { (void)destPath; }
};

// loadLedger() returning false marks a first run, which only records what the feed holds.
// Later runs download unseen books and stop at a fully seen page, MAX_PAGES or MAX_BOOKS_PER_SERVER.
void syncFeed(Backend& backend, const std::string& feedUrl, Summary& summary);

}  // namespace opdssync
