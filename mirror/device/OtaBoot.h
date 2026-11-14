#pragma once
#if defined(CROSSPOINT_MIRROR)
// Device side of the automatic fallback (ota/BootGuard.h): the record in NVS, the boot-time check and the rollback.
#include <cstdint>
#include <string>

namespace mirror::ota {

// Before the flash of a channel build `rev`: records that it is on trial and which slot holds the running image.
// False when the record could not be saved: the caller must then not flash.
bool armBootGuard(const std::string& rev);
// The flash failed after arming: nothing is on trial.
void disarmBootGuard();

// First thing in setup(). Counts a try of a build on trial; when it has used them all, switches the boot partition back
// to the old image and restarts. Changes nothing when no trial is recorded or anything is unreadable.
void bootGuardAtStart();
// Every pass of loop(): confirms the build on trial once it has run for kConfirmAfterMs.
void bootGuardTick(uint32_t uptimeMs);

// The last trial ended in a rollback (until the next install).
bool rolledBackNotice();

}  // namespace mirror::ota
#endif
