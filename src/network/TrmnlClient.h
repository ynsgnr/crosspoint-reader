#pragma once

#include <cstdint>

// TRMNL / LaraPaper device client: fetches the current screen into /sleep.bmp over an already connected Wi-Fi.
namespace trmnl {

enum class Result { Failed, Unchanged, Changed };

// A server URL is set.
bool configured();

// Joins the server on first use (empty token), then downloads the image when its filename changed.
Result refresh();

// Refresh rate the server asked for in the last successful refresh; 0 if unknown.
uint32_t lastRefreshSeconds();

}  // namespace trmnl
