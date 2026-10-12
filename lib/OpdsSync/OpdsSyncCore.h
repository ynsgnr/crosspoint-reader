#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "OpdsSeenLedger.h"

namespace opdssync {

constexpr uint8_t MAX_BOOKS_PER_SERVER = 5;
constexpr uint8_t MAX_PAGES = 3;

struct Summary {
  uint8_t servers = 0;     // servers with a sync feed that were attempted
  uint8_t downloaded = 0;  // books saved to SD
  uint8_t failed = 0;      // books or feeds that failed this run
  bool authFailed = false;
};

struct Book {
  std::string key;  // ledger key: the entry id, or its download link
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

// Everything the sync loop needs from the device, so the loop itself runs on a host.
class Backend {
 public:
  virtual ~Backend() = default;
  // False once the run deadline passed or the heap cannot take another TLS session.
  virtual bool canContinue() = 0;
  virtual Page fetchPage(const std::string& url) = 0;
  virtual std::string resolveUrl(const std::string& base, const std::string& href) = 0;
  virtual std::string destinationPath(const Book& book) = 0;
  virtual bool fileExists(const std::string& path) = 0;
  virtual DownloadResult download(const std::string& url, const std::string& destPath) = 0;
  // False when there is no usable ledger yet, which marks a first run.
  virtual bool loadLedger(OpdsSeenLedger& ledger) = 0;
  virtual void saveLedger(const OpdsSeenLedger& ledger) = 0;
  virtual void onBookSaved(const std::string& destPath) { (void)destPath; }
};

/**
 * Syncs one feed. The first run (no ledger) only records what the feed holds.
 * Later runs download the unseen books, newest first, and stop at the first
 * page whose books were all seen, at MAX_PAGES, or at MAX_BOOKS_PER_SERVER.
 */
void syncFeed(Backend& backend, const std::string& feedUrl, Summary& summary);

}  // namespace opdssync
