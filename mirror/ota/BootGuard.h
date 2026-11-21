#pragma once
// Automatic fallback for an installed channel build. The installer records "build <rev> is on trial, the old image is
// in slot <prev>" before it flashes. Every boot of that build counts a try; a build that stays up for kConfirmAfterMs
// is confirmed. One that has not confirmed after kMaxBootTries boots is replaced by the old image on the next boot. It
// runs inside the new build, so it cannot help one that fails before setup() runs, or the bootloader.
#include <cstddef>
#include <cstdint>
#include <string>

namespace mirror::ota {

struct BootGuard {
  bool pending = false;  // a freshly installed build has not confirmed itself yet
  uint8_t tries = 0;     // boots of the build on trial so far; past kMaxBootTries it counts failed rollback attempts
  uint8_t prevSlot = 0;  // app partition subtype of the image to go back to
  std::string rev;       // revision of the build on trial (MIRROR_GIT_REV)
};

constexpr uint8_t kMaxBootTries = 3;
constexpr uint8_t kMaxRollbackAttempts = 3;  // a switch that keeps failing is tried this many times, then given up
constexpr uint32_t kConfirmAfterMs = 10000;  // uptime, counted from boot

enum class BootAction {
  Nothing,   // no trial in progress
  Proceed,   // the build on trial runs, one more try used
  Rollback,  // it used all its tries without confirming: switch back to prevSlot, then endTrial()
  Stale,     // the record belongs to another build (the install was cut short, or an older firmware ran): dropped
};

// Fixed-size record kept in NVS (which checks its own integrity).
constexpr size_t kGuardBlobSize = 16;
void encodeGuard(const BootGuard& g, uint8_t out[kGuardBlobSize]);
// False for anything but a record this firmware wrote, so a foreign blob can never cause a rollback.
bool decodeGuard(const uint8_t* in, size_t len, BootGuard& out);

void armGuard(BootGuard& g, uint8_t prevSlot, const std::string& rev);  // before the flash
// The trial is over: the build confirmed, the rollback happened, the flash failed, or there is nothing to go back to.
void endTrial(BootGuard& g);
// `runningRev` is the revision this firmware was built from. Counts the try. A Rollback decision changes nothing by
// itself, so a boot that cannot finish the rollback decides again. A timer wake (a background sync that goes back to
// sleep within seconds, long before kConfirmAfterMs) is not a try and decides nothing: only a boot the user could see
// counts.
BootAction onBootGuard(BootGuard& g, const std::string& runningRev, bool timerWake = false);
bool guardConfirmDue(const BootGuard& g, uint32_t uptimeMs);
// The switch back failed. Counts the attempt and ends the trial after kMaxRollbackAttempts, so a boot loop cannot erase
// the otadata sector on every boot for ever. True when the trial is over.
bool rollbackFailedGuard(BootGuard& g);

}  // namespace mirror::ota
