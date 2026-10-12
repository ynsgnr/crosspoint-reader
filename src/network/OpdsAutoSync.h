#pragma once

#include <OpdsSyncCore.h>

#include <cstdint>

/**
 * Pulls new books from the OPDS feeds the user marked for auto-sync. The sync
 * loop itself lives in lib/OpdsSync (host-tested); this file binds it to the
 * device's HTTP, SD and settings. Blocking; the caller owns WiFi and any UI.
 */
namespace opdssync {

// True when the interval is set and at least one server has a sync feed.
bool enabled();

// Seconds between timer wakes; 0 when sync is off.
uint32_t intervalSeconds();

// Joins the last-used saved network. False on no credential or timeout.
bool joinSavedWifi(uint32_t timeoutMs);

// Runs every enabled server until done or `deadlineMs` (millis() timestamp).
Summary runAll(unsigned long deadlineMs);

// Writes a one-line result to /.crosspoint/opds_sync.txt.
void writeLog(const Summary& summary);

}  // namespace opdssync
