#include "TrmnlClient.h"

#include <ArduinoJson.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TrmnlProtocol.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "network/HttpDownloader.h"
#include "util/PluginHttp.h"

namespace trmnl {
namespace {

constexpr const char* STATE_PATH = "/.crosspoint/trmnl.json";
constexpr const char* IMAGE_PATH = "/sleep.bmp";
constexpr const char* TEMP_IMAGE_PATH = "/.crosspoint/trmnl_image.bmp";
constexpr size_t MAX_REPLY_BYTES = 4096;

struct State {
  std::string filename;
  uint32_t refreshSeconds = 0;
  int64_t checkedAt = 0;  // epoch of the last successful server check; 0 if unknown
  std::string error;      // why the last refresh failed; empty after a success
};

State loadState() {
  State state;
  JsonDocument doc;
  std::string json;
  if (Storage.readFileToString("TRM", STATE_PATH, MAX_REPLY_BYTES, json) && !deserializeJson(doc, json)) {
    state.filename = doc["filename"] | "";
    state.refreshSeconds = doc["refresh"].as<uint32_t>();
    state.checkedAt = doc["at"].as<int64_t>();
    state.error = doc["err"] | "";
  }
  return state;
}

void saveState(const State& state) {
  JsonDocument doc;
  doc["filename"] = state.filename;
  doc["refresh"] = state.refreshSeconds;
  doc["at"] = state.checkedAt;
  if (!state.error.empty()) doc["err"] = state.error;
  std::string json;
  serializeJson(doc, json);
  Storage.writeFile(STATE_PATH, String(json.c_str()));
}

bool heapFitsTls() {
  return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP &&
         ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC;
}

pluginhttp::Headers deviceHeaders() { return {{"ID", WiFi.macAddress().c_str()}, {"FW-Version", CROSSPOINT_VERSION}}; }

bool getJson(const std::string& url, const pluginhttp::Headers& headers, std::string& body, int& status) {
  String reply;
  status = pluginhttp::request(nullptr, url, "GET", "", headers, reply, MAX_REPLY_BYTES);
  if (status != 200) return false;
  body = reply.c_str();
  return true;
}

// BYOS servers such as LaraPaper hand out an access token to a device that announces its MAC address.
bool autoJoin(const std::string& server) {
  auto headers = deviceHeaders();
  if (SETTINGS.trmnlModelId[0]) headers.emplace_back("Model-Id", SETTINGS.trmnlModelId);
  std::string body;
  int status = 0;
  if (!getJson(server + "/api/setup", headers, body, status)) return false;
  const std::string token = parseSetupToken(body);
  if (token.empty() || token.size() >= sizeof(SETTINGS.trmnlToken)) return false;
  strlcpy(SETTINGS.trmnlToken, token.c_str(), sizeof(SETTINGS.trmnlToken));
  SETTINGS.saveToFile();
  return true;
}

bool isBmp(const char* path) {
  HalFile file;
  uint8_t magic[2] = {};
  return Storage.openFileForRead("TRM", path, file) && file.read(magic, sizeof(magic)) == 2 && magic[0] == 'B' &&
         magic[1] == 'M';
}

// SleepActivity caches a screen-sized conversion of /sleep.bmp as /.crosspoint/sleep_<hash>.bmp, keyed by size and
// modification time. Same-sized images written by the device share both, so a new image would show the old
// conversion. Dropping the conversions when an image arrives makes the next sleep convert it again.
void clearSleepConversions() {
  constexpr size_t MAX_CONVERSIONS = 16;
  std::vector<std::string> stale;
  {
    auto dir = Storage.open("/.crosspoint");
    if (!dir || !dir.isDirectory()) return;
    char name[96];
    for (auto entry = dir.openNextFile(); entry && stale.size() < MAX_CONVERSIONS; entry = dir.openNextFile()) {
      if (entry.isDirectory()) continue;
      entry.getName(name, sizeof(name));
      const size_t len = strlen(name);
      if (strncmp(name, "sleep_", 6) == 0 && len > 10 && strcmp(name + len - 4, ".bmp") == 0) stale.emplace_back(name);
    }
  }
  for (const auto& name : stale) Storage.remove(("/.crosspoint/" + name).c_str());
}

}  // namespace

bool configured() { return SETTINGS.trmnlServer[0] != '\0'; }

bool due(const uint32_t minimumMinutes) {
  if (!configured() || !Storage.exists(IMAGE_PATH)) return true;
  const State state = loadState();
  return refreshDue(trustedtime::trustedNow(), state.checkedAt, nextWakeSeconds(minimumMinutes, state.refreshSeconds));
}

uint32_t lastRefreshSeconds() { return configured() ? loadState().refreshSeconds : 0; }

Result refresh() {
  if (!configured()) return Result::Failed;
  State state = loadState();
  // A failed refresh leaves its reason in trmnl.json ("err"), so a stall can be read off the SD card.
  const auto fail = [&state](const std::string& why) {
    LOG_ERR("TRM", "Refresh failed: %s", why.c_str());
    state.error = why;
    saveState(state);
    return Result::Failed;
  };
  if (!heapFitsTls()) return fail("low heap");
  const std::string server = normalizeServerUrl(SETTINGS.trmnlServer);
  if (!SETTINGS.trmnlToken[0] && !autoJoin(server)) return fail("setup request failed");

  auto headers = deviceHeaders();
  headers.emplace_back("Access-Token", SETTINGS.trmnlToken);
  headers.emplace_back("Battery-Percent", std::to_string(powerManager.getBatteryPercentage()));
  headers.emplace_back("RSSI", std::to_string(WiFi.RSSI()));
  std::string body;
  int status = 0;
  if (!getJson(server + "/api/display", headers, body, status)) return fail("display http " + std::to_string(status));
  const DisplayReply reply = parseDisplayReply(body);
  if (!reply.ok) return fail("display reply rejected");

  state.error.clear();
  state.refreshSeconds = reply.refreshSeconds;
  state.checkedAt = trustedtime::trustedNow();
  const bool unchanged = !reply.filename.empty() && reply.filename == state.filename && Storage.exists(IMAGE_PATH);
  if (unchanged) {
    saveState(state);
    return Result::Unchanged;
  }

  if (!heapFitsTls()) return fail("low heap before download");
  Storage.remove(TEMP_IMAGE_PATH);
  if (HttpDownloader::downloadToFile(reply.imageUrl, TEMP_IMAGE_PATH) != HttpDownloader::OK ||
      !isBmp(TEMP_IMAGE_PATH) || !Storage.replaceFile(TEMP_IMAGE_PATH, IMAGE_PATH)) {
    Storage.remove(TEMP_IMAGE_PATH);
    return fail("image download or save failed");
  }
  clearSleepConversions();
  state.filename = reply.filename;
  saveState(state);
  return Result::Changed;
}

}  // namespace trmnl
