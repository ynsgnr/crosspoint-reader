#pragma once

#include <cstdint>

/**
 * Pulls new books from the OPDS feeds the user marked for auto-sync. Each
 * server with a sync feed gets a seen-ledger; the first run only records what
 * the feed already holds, later runs download the unseen EPUBs (newest-first
 * feed assumed). Blocking; the caller owns WiFi and any UI.
 */
namespace opdssync {

struct Summary {
  uint8_t servers = 0;     // servers with a sync feed that were attempted
  uint8_t downloaded = 0;  // books saved to SD
  uint8_t failed = 0;      // books or feeds that failed this run
  bool authFailed = false;
};

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
