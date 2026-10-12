#include "OpdsSyncCore.h"

namespace opdssync {

void syncFeed(Backend& backend, const std::string& feedUrl, Summary& summary) {
  OpdsSeenLedger ledger;
  const bool baseline = !backend.loadLedger(ledger);

  std::string pageUrl = feedUrl;
  uint8_t budget = MAX_BOOKS_PER_SERVER;
  bool ledgerChanged = false;

  for (uint8_t pageIndex = 0; pageIndex < MAX_PAGES && !pageUrl.empty(); pageIndex++) {
    if (!backend.canContinue()) {
      summary.failed++;
      break;
    }

    const Page page = backend.fetchPage(pageUrl);
    if (page.status != Page::Status::Ok) {
      summary.failed++;
      summary.authFailed |= page.status == Page::Status::AuthFailed;
      break;
    }

    std::vector<std::string> keys;
    keys.reserve(page.books.size());
    for (const auto& book : page.books) keys.push_back(book.key);

    bool allSeen = false;
    std::vector<size_t> toFetch;
    if (baseline) {
      for (const auto& key : keys) ledger.add(key);
      ledgerChanged = true;
    } else {
      const OpdsPagePlan plan = planOpdsPage(keys, ledger, budget);
      allSeen = plan.allSeen;
      toFetch = plan.fetch;
    }

    for (const size_t i : toFetch) {
      const Book& book = page.books[i];
      if (!backend.canContinue()) {
        summary.failed++;
        budget = 0;
        break;
      }
      const std::string dest = backend.destinationPath(book);
      if (backend.fileExists(dest)) {  // Already on the card: just remember it.
        ledger.add(book.key);
        ledgerChanged = true;
        continue;
      }
      const DownloadResult result = backend.download(backend.resolveUrl(pageUrl, book.href), dest);
      if (result == DownloadResult::Ok) {
        backend.onBookSaved(dest);
        ledger.add(book.key);
        ledgerChanged = true;
        summary.downloaded++;
        budget--;
      } else {
        summary.failed++;
        if (result == DownloadResult::Unauthorized) {
          summary.authFailed = true;
          budget = 0;
          break;
        }
      }
    }

    if (allSeen || budget == 0) break;
    pageUrl = page.nextHref.empty() ? std::string() : backend.resolveUrl(pageUrl, page.nextHref);
  }

  if (ledgerChanged) backend.saveLedger(ledger);
}

}  // namespace opdssync
