#pragma once
// Trigger chord: modifiers + one key, sent over HID to make the phone start the mirror. Portable and unit tested.
#include <cstdint>
#include <string>

namespace mirror {

constexpr uint8_t kModCtrl = 1, kModShift = 2, kModAlt = 4, kModMeta = 8;  // also the protocol's chord mask

struct Chord {
  uint8_t mods;
  uint8_t key;  // HID keyboard usage id: A-Z 0x04-0x1D, 1-9 0x1E-0x26, 0 0x27
  bool operator==(const Chord& o) const { return mods == o.mods && key == o.key; }
};

constexpr Chord kDefaultChord{kModCtrl | kModAlt | kModShift, 0x10};  // Ctrl+Alt+Shift+M

enum class ChordCheck { Ok, TooFewModifiers, MetaReserved, KeyNotAllowed, CommonShortcut };

// A chord must have at least two modifiers (never a single key), no Meta (Android reserves it), a letter or digit key,
// and must not be a well-known two-modifier shortcut. Best effort: confirm it on the phone in use.
ChordCheck checkChord(const Chord& c);
const char* chordCheckText(ChordCheck c);

std::string chordLabel(const Chord& c);       // "Ctrl+Alt+Shift+M"
uint8_t nextChordKey(uint8_t key, int step);  // cycles A-Z then 1-0

// HID usage for a digit 0-9 on the main row.
constexpr uint8_t digitUsage(int digit) { return digit == 0 ? 0x27 : static_cast<uint8_t>(0x1D + digit); }

}  // namespace mirror
