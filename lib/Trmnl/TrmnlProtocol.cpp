#include "TrmnlProtocol.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace trmnl {
namespace {

constexpr uint32_t MIN_SERVER_WAKE_SECONDS = 300;
constexpr uint32_t MAX_SERVER_WAKE_SECONDS = 86400;

bool isSuccessStatus(JsonVariantConst status) {
  if (status.isNull()) return true;
  const int code = status.as<int>();
  return code == 0 || code == 200;
}

}  // namespace

DisplayReply parseDisplayReply(const std::string& json) {
  DisplayReply reply;
  JsonDocument doc;
  if (deserializeJson(doc, json) || !isSuccessStatus(doc["status"])) return reply;
  reply.imageUrl = doc["image_url"] | "";
  reply.filename = doc["filename"] | "";
  reply.refreshSeconds = doc["refresh_rate"].as<uint32_t>();
  reply.ok = !reply.imageUrl.empty();
  return reply;
}

std::string parseSetupToken(const std::string& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return "";
  return doc["api_key"] | "";
}

std::string normalizeServerUrl(const std::string& url) {
  std::string result = url;
  while (!result.empty() && result.back() == '/') result.pop_back();
  if (!result.empty() && result.find("://") == std::string::npos) result.insert(0, "https://");
  return result;
}

uint32_t nextWakeSeconds(const uint32_t minimumMinutes, const uint32_t serverSeconds) {
  const uint32_t serverWake =
      serverSeconds == 0 ? 0 : std::clamp(serverSeconds, MIN_SERVER_WAKE_SECONDS, MAX_SERVER_WAKE_SECONDS);
  return std::max(minimumMinutes * 60, serverWake);
}

}  // namespace trmnl
