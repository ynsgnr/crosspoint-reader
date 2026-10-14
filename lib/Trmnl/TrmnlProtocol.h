#pragma once

#include <cstdint>
#include <string>

// Parsing and timing for the TRMNL device protocol (TRMNL cloud and BYOS servers such as LaraPaper).
namespace trmnl {

struct DisplayReply {
  bool ok = false;
  std::string imageUrl;
  std::string filename;
  uint32_t refreshSeconds = 0;
};

// Reply of GET /api/display. ok needs a success status and an image URL.
DisplayReply parseDisplayReply(const std::string& json);

// Access token from the reply of GET /api/setup; empty when missing.
std::string parseSetupToken(const std::string& json);

// Adds https:// when no scheme is given and drops trailing slashes.
std::string normalizeServerUrl(const std::string& url);

// Seconds until the next wake: the larger of the user's minimum and the server's refresh rate, kept within
// 5 minutes to 24 hours. 0 when both are 0.
uint32_t nextWakeSeconds(uint32_t minimumMinutes, uint32_t serverSeconds);

}  // namespace trmnl
