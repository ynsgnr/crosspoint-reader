#pragma once

#include <OpdsSyncCore.h>

#include <cstdint>

// Binds the host-tested sync loop in lib/OpdsSync to the device's HTTP, SD card and settings.
namespace opdssync {

// At least one server has a sync feed.
bool hasSyncFeed();

// The interval is set and at least one server has a sync feed.
bool enabled();

// Seconds between timer wakes; 0 when sync is off.
uint32_t intervalSeconds();

// Blocking. Joins the last used WiFi network, syncs every server that has a
// feed, and records the result in /.crosspoint/opds_sync.txt.
Summary syncNow();

}  // namespace opdssync
