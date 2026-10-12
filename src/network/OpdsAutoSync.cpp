#include "OpdsAutoSync.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <OpdsParser.h>
#include <OpdsSeenLedger.h>
#include <OpdsStream.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include <algorithm>
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

constexpr const char* RESULT_LOG_PATH = "/.crosspoint/opds_sync.txt";
constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
constexpr unsigned long RUN_DEADLINE_MS = 180000;

// Keyed by server and feed, so changing the feed starts a fresh baseline.
std::string ledgerPath(const OpdsServer& server) {
  char path[48];
  const uint32_t id = static_cast<uint32_t>(OpdsSeenLedger::hashKey(server.url + '|' + server.syncFeed));
  snprintf(path, sizeof(path), "/.crosspoint/opds_seen_%08x.bin", static_cast<unsigned>(id));
  return path;
}

std::string entryKey(const OpdsEntry& entry) { return entry.id.empty() ? entry.href : entry.id; }

bool heapFitsTls() {
  return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP &&
         ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC;
}

class DeviceBackend final : public Backend {
 public:
  DeviceBackend(const OpdsServer& server, const unsigned long deadlineMs)
      : server(server), deadlineMs(deadlineMs), ledgerFile(ledgerPath(server)) {}

  bool canContinue() override { return static_cast<long>(millis() - deadlineMs) < 0 && heapFitsTls(); }

  Page fetchPage(const std::string& url) override {
    Page page;
    OpdsParser parser;  // freed on return, before downloads take the heap for TLS
    {
      OpdsParserStream stream{parser};
      if (!HttpDownloader::fetchUrl(url, stream, server.username, server.password)) {
        const int status = HttpDownloader::lastStatus();
        page.status = (status == 401 || status == 403) ? Page::Status::AuthFailed : Page::Status::Failed;
        return page;
      }
    }
    if (!parser) {
      page.status = Page::Status::Failed;
      return page;
    }
    for (const auto& entry : parser.getEntries()) {
      if (entry.type != OpdsEntryType::BOOK) continue;
      page.books.push_back({entryKey(entry), entry.title, entry.author, entry.href});
    }
    page.nextHref = parser.getNextPageUrl();
    return page;
  }

  std::string resolveUrl(const std::string& base, const std::string& href) override {
    return UrlUtils::buildUrl(base, href);
  }

  std::string destinationPath(const Book& book) override {
    const char* folder = SETTINGS.opdsDownloadFolder;
    bool haveFolder = folder[0] != '\0';
    if (haveFolder && !Storage.exists(folder) && !Storage.mkdir(folder)) haveFolder = false;
    std::string path;
    if (haveFolder) path += folder;
    path += '/';
    path += opdsBookFilename(book.author, book.title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
    return path;
  }

  bool fileExists(const std::string& path) override { return Storage.exists(path.c_str()); }

  DownloadResult download(const std::string& url, const std::string& destPath) override {
    const auto result =
        HttpDownloader::downloadToFile(url, destPath, nullptr, nullptr, server.username, server.password);
    if (result == HttpDownloader::OK) return DownloadResult::Ok;
    LOG_ERR("OSY", "Download failed (%d): %s", static_cast<int>(result), destPath.c_str());
    return result == HttpDownloader::UNAUTHORIZED ? DownloadResult::Unauthorized : DownloadResult::Failed;
  }

  bool loadLedger(OpdsSeenLedger& ledger) override {
    HalFile file;
    if (!Storage.openFileForRead("OSY", ledgerFile, file)) return false;
    const size_t size = file.size();
    if (size == 0 || size > OpdsSeenLedger::MAX_SERIALIZED_BYTES) return false;
    std::vector<uint8_t> bytes(size);
    if (file.read(bytes.data(), size) != static_cast<int>(size)) return false;
    return ledger.deserialize(bytes.data(), size);
  }

  void saveLedger(const OpdsSeenLedger& ledger) override {
    std::vector<uint8_t> bytes;
    ledger.serialize(bytes);
    HalFile file;
    if (!Storage.openFileForWrite("OSY", ledgerFile, file)) {
      LOG_ERR("OSY", "Cannot write ledger %s", ledgerFile.c_str());
      return;
    }
    file.write(bytes.data(), bytes.size());
  }

  void onBookSaved(const std::string& destPath) override { clearBookCache(destPath); }

 private:
  const OpdsServer& server;
  const unsigned long deadlineMs;
  const std::string ledgerFile;
};

bool joinSavedWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  const auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!credential) return false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  const unsigned long deadlineMs = millis() + WIFI_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadlineMs) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

void writeResultLog(const Summary& summary) {
  char line[96];
  snprintf(line, sizeof(line), "t=%lld servers=%u downloaded=%u failed=%u auth_failed=%u wifi_failed=%u\n",
           static_cast<long long>(time(nullptr)), summary.servers, summary.downloaded, summary.failed,
           summary.authFailed ? 1u : 0u, summary.wifiFailed ? 1u : 0u);
  Storage.writeFile(RESULT_LOG_PATH, String(line));
}

}  // namespace

bool hasSyncFeed() {
  const auto& servers = OPDS_STORE.getServers();
  return std::any_of(servers.begin(), servers.end(), [](const OpdsServer& s) { return !s.syncFeed.empty(); });
}

bool enabled() { return SETTINGS.opdsSyncHours != 0 && hasSyncFeed(); }

uint32_t intervalSeconds() {
  if (!enabled()) return 0;
#ifdef OPDS_SYNC_TEST_MINUTES
  return static_cast<uint32_t>(SETTINGS.opdsSyncHours) * 60;  // bench builds: hours become minutes
#else
  return static_cast<uint32_t>(SETTINGS.opdsSyncHours) * 3600;
#endif
}

Summary syncNow() {
  Summary summary;
  if (!joinSavedWifi()) {
    summary.wifiFailed = true;
    writeResultLog(summary);
    return summary;
  }

  trustedtime::startSync();
  const unsigned long deadlineMs = millis() + RUN_DEADLINE_MS;
  const std::vector<OpdsServer> servers = OPDS_STORE.getServers();  // copy: the store can reload mid-run
  for (const auto& server : servers) {
    if (server.syncFeed.empty() || server.url.empty()) continue;
    summary.servers++;
    LOG_INF("OSY", "Syncing %s", server.name.c_str());
    DeviceBackend backend(server, deadlineMs);
    syncFeed(backend, UrlUtils::buildUrl(server.url, server.syncFeed), summary);
  }
  if (summary.downloaded > 0) library::markLibraryIndexDirty();
  writeResultLog(summary);
  return summary;
}

}  // namespace opdssync
