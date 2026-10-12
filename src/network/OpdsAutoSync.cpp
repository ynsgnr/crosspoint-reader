#include "OpdsAutoSync.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <OpdsParser.h>
#include <OpdsSeenLedger.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include <ctime>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"
#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/UrlUtils.h"

namespace opdssync {
namespace {

constexpr uint8_t MAX_BOOKS_PER_SERVER = 5;
constexpr uint8_t MAX_PAGES = 3;
constexpr const char* LOG_PATH = "/.crosspoint/opds_sync.txt";

struct Pending {
  std::string key;
  std::string title;
  std::string author;
  std::string href;
};

// One ledger per server and feed: changing the feed starts a fresh baseline.
std::string ledgerPath(const OpdsServer& server) {
  char path[48];
  const uint32_t id = static_cast<uint32_t>(OpdsSeenLedger::hashKey(server.url + '|' + server.syncFeed));
  snprintf(path, sizeof(path), "/.crosspoint/opds_seen_%08x.bin", static_cast<unsigned>(id));
  return path;
}

// False when the file is absent or unreadable, which marks a first run.
bool loadLedger(const std::string& path, OpdsSeenLedger& ledger) {
  HalFile file;
  if (!Storage.openFileForRead("OSY", path, file)) return false;
  const size_t size = file.size();
  if (size == 0 || size > OpdsSeenLedger::MAX_SERIALIZED_BYTES) return false;
  std::vector<uint8_t> bytes(size);
  if (file.read(bytes.data(), size) != static_cast<int>(size)) return false;
  return ledger.deserialize(bytes.data(), size);
}

void saveLedger(const std::string& path, const OpdsSeenLedger& ledger) {
  std::vector<uint8_t> bytes;
  ledger.serialize(bytes);
  HalFile file;
  if (!Storage.openFileForWrite("OSY", path, file)) {
    LOG_ERR("OSY", "Cannot write ledger %s", path.c_str());
    return;
  }
  file.write(bytes.data(), bytes.size());
}

// The entry id is the stable identity; fall back to the download link.
std::string entryKey(const OpdsEntry& entry) { return entry.id.empty() ? entry.href : entry.id; }

bool heapFitsTls() {
  return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP &&
         ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC;
}

std::string destinationPath(const Pending& book) {
  const char* folder = SETTINGS.opdsDownloadFolder;
  bool haveFolder = folder[0] != '\0';
  if (haveFolder && !Storage.exists(folder) && !Storage.mkdir(folder)) haveFolder = false;
  std::string path;
  if (haveFolder) path += folder;
  path += '/';
  path += opdsBookFilename(book.author, book.title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  return path;
}

enum class Fetch { Ok, Failed, AuthFailed };

Fetch fetchPage(const OpdsServer& server, const std::string& url, OpdsParser& parser) {
  OpdsParserStream stream{parser};
  if (!HttpDownloader::fetchUrl(url, stream, server.username, server.password)) {
    const int status = HttpDownloader::lastStatus();
    return (status == 401 || status == 403) ? Fetch::AuthFailed : Fetch::Failed;
  }
  return parser ? Fetch::Ok : Fetch::Failed;
}

void syncServer(const OpdsServer& server, const unsigned long deadlineMs, Summary& summary) {
  summary.servers++;
  const std::string path = ledgerPath(server);
  OpdsSeenLedger ledger;
  const bool baseline = !loadLedger(path, ledger);
  LOG_INF("OSY", "Syncing %s (%s)", server.name.c_str(), baseline ? "baseline" : "incremental");

  std::string pageUrl = UrlUtils::buildUrl(server.url, server.syncFeed);
  uint8_t budget = MAX_BOOKS_PER_SERVER;
  bool ledgerChanged = false;

  for (uint8_t page = 0; page < MAX_PAGES && !pageUrl.empty(); page++) {
    if (static_cast<long>(millis() - deadlineMs) >= 0 || !heapFitsTls()) {
      summary.failed++;
      break;
    }

    std::vector<Pending> pending;
    std::string nextUrl;
    bool allSeen = false;
    {
      OpdsParser parser;
      const Fetch fetched = fetchPage(server, pageUrl, parser);
      if (fetched != Fetch::Ok) {
        summary.failed++;
        summary.authFailed |= fetched == Fetch::AuthFailed;
        break;
      }

      std::vector<std::string> keys;
      std::vector<const OpdsEntry*> books;
      for (const auto& entry : parser.getEntries()) {
        if (entry.type != OpdsEntryType::BOOK) continue;
        keys.push_back(entryKey(entry));
        books.push_back(&entry);
      }

      if (baseline) {
        for (const auto& key : keys) ledger.add(key);
        ledgerChanged = true;
      } else {
        const OpdsPagePlan plan = planOpdsPage(keys, ledger, budget);
        allSeen = plan.allSeen;
        for (const size_t i : plan.fetch)
          pending.push_back({keys[i], books[i]->title, books[i]->author, books[i]->href});
      }
      nextUrl = parser.getNextPageUrl();
    }  // Parser (and its entries) freed before TLS takes the heap for downloads.

    for (const auto& book : pending) {
      if (static_cast<long>(millis() - deadlineMs) >= 0 || !heapFitsTls()) {
        summary.failed++;
        budget = 0;
        break;
      }
      const std::string dest = destinationPath(book);
      const std::string url = UrlUtils::buildUrl(pageUrl, book.href);
      if (Storage.exists(dest.c_str())) {  // Already on the card: just remember it.
        ledger.add(book.key);
        ledgerChanged = true;
        continue;
      }
      const auto result = HttpDownloader::downloadToFile(url, dest, nullptr, nullptr, server.username, server.password);
      if (result == HttpDownloader::OK) {
        clearBookCache(dest);
        ledger.add(book.key);
        ledgerChanged = true;
        summary.downloaded++;
        budget--;
      } else {
        LOG_ERR("OSY", "Download failed (%d): %s", static_cast<int>(result), book.title.c_str());
        summary.failed++;
        summary.authFailed |= result == HttpDownloader::UNAUTHORIZED;
        if (result == HttpDownloader::UNAUTHORIZED) {
          budget = 0;
          break;
        }
      }
    }

    if (allSeen || budget == 0) break;
    pageUrl = nextUrl.empty() ? nextUrl : UrlUtils::buildUrl(pageUrl, nextUrl);
  }

  if (ledgerChanged) saveLedger(path, ledger);
}

}  // namespace

bool enabled() {
  if (SETTINGS.opdsSyncHours == 0) return false;
  for (const auto& server : OPDS_STORE.getServers()) {
    if (!server.syncFeed.empty()) return true;
  }
  return false;
}

uint32_t intervalSeconds() {
  if (!enabled()) return 0;
#ifdef OPDS_SYNC_TEST_MINUTES
  return static_cast<uint32_t>(SETTINGS.opdsSyncHours) * 60;  // bench builds: hours become minutes
#else
  return static_cast<uint32_t>(SETTINGS.opdsSyncHours) * 3600;
#endif
}

bool joinSavedWifi(const uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  const auto cred = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!cred) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  const unsigned long deadline = millis() + timeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

Summary runAll(const unsigned long deadlineMs) {
  Summary summary;
  // Copy: a store reload mid-run must not invalidate the server being synced.
  const std::vector<OpdsServer> servers = OPDS_STORE.getServers();
  for (const auto& server : servers) {
    if (server.syncFeed.empty() || server.url.empty()) continue;
    syncServer(server, deadlineMs, summary);
  }
  if (summary.downloaded > 0) library::markLibraryIndexDirty();
  return summary;
}

void writeLog(const Summary& summary) {
  char line[96];
  snprintf(line, sizeof(line), "t=%lld servers=%u downloaded=%u failed=%u auth_failed=%u\n",
           static_cast<long long>(time(nullptr)), summary.servers, summary.downloaded, summary.failed,
           summary.authFailed ? 1u : 0u);
  Storage.writeFile(LOG_PATH, String(line));
}

}  // namespace opdssync
