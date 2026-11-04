#pragma once
#if defined(CROSSPOINT_MIRROR)
// The Mirror module's own persisted settings (NVS namespace "mirror"). Nothing about a PIN is ever stored.
#include "core/Chord.h"

namespace mirror::settings {

// Opt-in HID keyboard. Changing it changes the GATT database, so the caller also forgets the bond.
bool keyboardEnabled();
void setKeyboardEnabled(bool on);

// True when bonds were made under security model `epoch`; otherwise records it and returns false.
bool securityEpochCurrent(uint8_t epoch);

// Trigger chord; an invalid stored value falls back to the default.
Chord chord();
void setChord(const Chord& c);

}  // namespace mirror::settings
#endif
