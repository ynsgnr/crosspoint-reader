#pragma once
// Automatic fallback for an installed channel build. The installer records "build <rev> is on trial, the old image is
// in slot <prev>" before it flashes. Every boot of that build counts a try (before setup(), see
// device/OtaBootHooks.cpp); a build that reaches its main loop and stays up for kConfirmAfterMs is confirmed. A build
// that does not confirm within kMaxBootTries boots is replaced by the old image on the next boot. It cannot help a
// build that fails before setup() runs, or the bootloader.
#include <cstddef>
#include <cstdint>
#include <string>

namespace mirror::ota {

struct BootGuard {
  bool pending = false;     // a freshly installed build has not confirmed itself yet
  bool rolledBack = false;  // the last trial ended in a rollback (shown on the Firmware channels screen)
  uint8_t tries = 0;        // boots of the build on trial so far
  uint8_t prevSlot = 0;     // app partition subtype of the image to go back to
  std::string rev;          // revision of the build on trial (MIRROR_GIT_REV)
};

constexpr uint8_t kMaxBootTries = 3;
constexpr uint32_t kConfirmAfterMs = 10000;  // uptime, counted from boot

// What the boot-time check decided.
enum class BootAction {
  Nothing,   // no trial in progress
  Proceed,   // the build on trial runs, one more try used
  Rollback,  // it used all its tries without confirming: switch back to prevSlot. The record is left as it is, so a
             // boot that cannot finish the rollback (or a failed switch) decides again; report the outcome with
             // rolledBackGuard/giveUpGuard
  Stale,     // the record belongs to another build (the install was cut short, or an older firmware ran): dropped
};

// Fixed-size record kept in NVS.
constexpr size_t kGuardBlobSize = 24;
void encodeGuard(const BootGuard& g, uint8_t out[kGuardBlobSize]);
// False for anything that is not a record this firmware wrote (length, version, checksum, revision text).
bool decodeGuard(const uint8_t* in, size_t len, BootGuard& out);

void armGuard(BootGuard& g, uint8_t prevSlot, const std::string& rev);  // before the flash
void disarmGuard(BootGuard& g);                                         // the flash failed: nothing is on trial
// `runningRev` is the revision this firmware was built from. Counts the try.
BootAction onBootGuard(BootGuard& g, const std::string& runningRev);
bool guardConfirmDue(const BootGuard& g, uint32_t uptimeMs);
void confirmGuard(BootGuard& g);
// The boot partition was switched back: the trial is over and the screen may say so.
void rolledBackGuard(BootGuard& g);
// There is nothing to go back to (the old image is gone): the trial is over, quietly.
void giveUpGuard(BootGuard& g);

}  // namespace mirror::ota
