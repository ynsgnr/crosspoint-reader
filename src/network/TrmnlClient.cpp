#include "TrmnlClient.h"

#include <ArduinoJson.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TrmnlProtocol.h>
#include <WiFi.h>

#include <string>

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
};

State loadState() {
  State state;
  JsonDocument doc;
  std::string json;
  if (Storage.readFileToString("TRM", STATE_PATH, MAX_REPLY_BYTES, json) && !deserializeJson(doc, json)) {
    state.filename = doc["filename"] | "";
    state.refreshSeconds = doc["refresh"].as<uint32_t>();
  }
  return state;
}

void saveState(const State& state) {
  JsonDocument doc;
  doc["filename"] = state.filename;
  doc["refresh"] = state.refreshSeconds;
  std::string json;
  serializeJson(doc, json);
  Storage.writeFile(STATE_PATH, String(json.c_str()));
}

bool heapFitsTls() {
  return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP &&
         ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC;
}

pluginhttp::Headers deviceHeaders() { return {{"ID", WiFi.macAddress().c_str()}, {"FW-Version", CROSSPOINT_VERSION}}; }

bool getJson(const std::string& url, const pluginhttp::Headers& headers, std::string& body) {
  String reply;
  if (pluginhttp::request(nullptr, url, "GET", "", headers, reply, MAX_REPLY_BYTES) != 200) return false;
  body = reply.c_str();
  return true;
}

// LaraPaper hands out an access token to any device that announces its MAC address.
bool autoJoin(const std::string& server) {
  auto headers = deviceHeaders();
  if (SETTINGS.trmnlModelId[0]) headers.emplace_back("Model-Id", SETTINGS.trmnlModelId);
  std::string body;
  if (!getJson(server + "/api/setup", headers, body)) return false;
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

}  // namespace

bool configured() { return SETTINGS.trmnlServer[0] != '\0'; }

uint32_t lastRefreshSeconds() { return loadState().refreshSeconds; }

Result refresh() {
  if (!configured() || !heapFitsTls()) return Result::Failed;
  const std::string server = normalizeServerUrl(SETTINGS.trmnlServer);
  if (!SETTINGS.trmnlToken[0] && !autoJoin(server)) return Result::Failed;

  auto headers = deviceHeaders();
  headers.emplace_back("Access-Token", SETTINGS.trmnlToken);
  headers.emplace_back("Battery-Percent", std::to_string(powerManager.getBatteryPercentage()));
  headers.emplace_back("RSSI", std::to_string(WiFi.RSSI()));
  std::string body;
  if (!getJson(server + "/api/display", headers, body)) return Result::Failed;
  const DisplayReply reply = parseDisplayReply(body);
  if (!reply.ok) return Result::Failed;

  State state = loadState();
  state.refreshSeconds = reply.refreshSeconds;
  const bool unchanged = !reply.filename.empty() && reply.filename == state.filename && Storage.exists(IMAGE_PATH);
  if (unchanged) {
    saveState(state);
    return Result::Unchanged;
  }

  if (!heapFitsTls()) return Result::Failed;
  Storage.remove(TEMP_IMAGE_PATH);
  if (HttpDownloader::downloadToFile(reply.imageUrl, TEMP_IMAGE_PATH) != HttpDownloader::OK ||
      !isBmp(TEMP_IMAGE_PATH) || !Storage.replaceFile(TEMP_IMAGE_PATH, IMAGE_PATH)) {
    LOG_ERR("TRM", "Image download failed");
    Storage.remove(TEMP_IMAGE_PATH);
    return Result::Failed;
  }
  state.filename = reply.filename;
  saveState(state);
  return Result::Changed;
}

}  // namespace trmnl
