#pragma once

#include <OpdsSyncCore.h>

#include <cstdint>

// Binds the host-tested sync loop in lib/OpdsSync to the device's HTTP, SD card and settings.
namespace opdssync {

// At least one server has a sync feed.
bool hasSyncFeed();

// The interval is set and at least one server has a sync feed.
bool enabled();

// Joins the last used saved Wi-Fi network. True when connected.
bool joinWifi();

// A second way online for a sync, tried when no saved Wi-Fi network can be joined. The mirror build offers the phone's
// mobile data (mirror/device/PhoneRoute.cpp). open() returns true once Wi-Fi is up; it leaves nothing running when it
// fails.
class NetworkFallback {
 public:
  virtual ~NetworkFallback() = default;
  virtual bool open() = 0;
};
void setNetworkFallback(NetworkFallback* fallback);

// joinWifi(), then the fallback when that fails. For the syncs the timer and "Sync Now" ask for, not for the joins at
// sleep.
bool joinWifiOrFallback();

// Blocking. Joins the last used WiFi network, syncs every server that has a
// feed, and records the result in /.crosspoint/opds_sync.txt.
Summary syncNow();

}  // namespace opdssync
