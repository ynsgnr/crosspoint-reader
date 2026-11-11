#pragma once

#include <cstdint>

// TRMNL / LaraPaper client: fetches the current screen into /sleep.bmp over a connected Wi-Fi.
namespace trmnl {

enum class Result { Failed, Unchanged, Changed };

// A server URL is set.
bool configured();

// Joins the server on first use (empty token), then downloads the image when its filename changed.
Result refresh();

// True when /sleep.bmp is older than the refresh period (the larger of `minimumMinutes` and the server's rate), or its
// age is unknown. A sleep that finds it fresh skips the network.
bool due(uint32_t minimumMinutes);

// Refresh rate the server asked for in the last successful refresh; 0 if unknown.
uint32_t lastRefreshSeconds();

}  // namespace trmnl
