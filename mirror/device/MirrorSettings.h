#pragma once
#if defined(CROSSPOINT_MIRROR)
// The Mirror module's own persisted settings (NVS namespace "mirror"). Nothing about a PIN is ever stored.
#include <cstdint>

namespace mirror::settings {

// True when the stored security model is missing or differs from `epoch`: bonds made under an older model must go.
// An NVS that cannot be read says nothing, so it is not stale (the bonds stay).
bool securityEpochStale(uint8_t epoch);
// Records `epoch` (after stale bonds were deleted).
void commitSecurityEpoch(uint8_t epoch);

}  // namespace mirror::settings
#endif
