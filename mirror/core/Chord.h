#pragma once
// Trigger chord: the fixed key combination typed over HID to make the phone start the mirror (the phone's Chord.DEFAULT
// matches it). Portable.
#include <cstdint>

namespace mirror {

constexpr uint8_t kModCtrl = 1, kModShift = 2, kModAlt = 4;  // HID modifier bits used here

struct Chord {
  uint8_t mods;
  uint8_t key;  // HID keyboard usage id
};

constexpr Chord kDefaultChord{kModCtrl | kModAlt | kModShift, 0x10};  // Ctrl+Alt+Shift+M

// HID usage for a digit 0-9 on the main row.
constexpr uint8_t digitUsage(int digit) { return digit == 0 ? 0x27 : static_cast<uint8_t>(0x1D + digit); }

}  // namespace mirror
