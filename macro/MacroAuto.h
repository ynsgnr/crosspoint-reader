#pragma once
#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
// Opens Macro by itself when a PC is attached over USB (X4 Pro). Home calls shouldOpen() each loop. See
// macro/README.md.
#include "MacroUsb.h"

namespace macro::autoopen {

#if MACRO_HAS_USB
// The "Auto-open on PC" setting (NVS namespace "macro"). Off unless set.
bool enabled();
void setEnabled(bool on);

// True once, when Macro should open now. `userActive` is any button held or touch since the last call. Opens only if
// the setting is on, a PC (not a charger) has sent USB frames for a few seconds, the user has not touched anything
// since boot, and Macro was not just closed with the cable still in.
bool shouldOpen(bool userActive);

// Called when Macro closes: no auto-open again until the PC has been unplugged for a moment. Survives the restart.
void suppressUntilUnplug();
#else
inline bool enabled() { return false; }
inline void setEnabled(bool) {}
inline bool shouldOpen(bool) { return false; }
inline void suppressUntilUnplug() {}
#endif

}  // namespace macro::autoopen
#endif
