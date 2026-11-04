#include "core/Chord.h"

#include <cstring>

namespace mirror {
namespace {

bool isAllowedKey(uint8_t key) { return key >= 0x04 && key <= 0x27; }  // letters, then 1-9 and 0

char keyChar(uint8_t key) {
  if (key >= 0x04 && key <= 0x1D) return static_cast<char>('A' + (key - 0x04));
  if (key >= 0x1E && key <= 0x26) return static_cast<char>('1' + (key - 0x1E));
  return '0';
}

int modCount(uint8_t m) { return ((m & kModCtrl) != 0) + ((m & kModShift) != 0) + ((m & kModAlt) != 0) + ((m & kModMeta) != 0); }

// Two-modifier shortcuts that browsers, terminals and Android itself commonly use. Three modifiers are free.
bool commonShortcut(uint8_t mods, uint8_t key) {
  const char k = keyChar(key);
  if (mods == (kModCtrl | kModShift)) return std::strchr("BDHIJNOPQRTWZ", k) != nullptr;
  if (mods == (kModCtrl | kModAlt)) return std::strchr("DLTZ", k) != nullptr;
  if (mods == (kModAlt | kModShift)) return std::strchr("DLNT", k) != nullptr;
  return false;
}

}  // namespace

ChordCheck checkChord(const Chord& c) {
  if (modCount(c.mods) < 2) return ChordCheck::TooFewModifiers;
  if (c.mods & kModMeta) return ChordCheck::MetaReserved;
  if (!isAllowedKey(c.key)) return ChordCheck::KeyNotAllowed;
  if (commonShortcut(c.mods, c.key)) return ChordCheck::CommonShortcut;
  return ChordCheck::Ok;
}

const char* chordCheckText(ChordCheck c) {
  switch (c) {
    case ChordCheck::Ok: return "";
    case ChordCheck::TooFewModifiers: return "Use at least two modifiers";
    case ChordCheck::MetaReserved: return "Meta is reserved by Android";
    case ChordCheck::KeyNotAllowed: return "Use a letter or digit";
    case ChordCheck::CommonShortcut: return "Common shortcut: pick another";
  }
  return "";
}

std::string chordLabel(const Chord& c) {
  std::string s;
  if (c.mods & kModCtrl) s += "Ctrl+";
  if (c.mods & kModAlt) s += "Alt+";
  if (c.mods & kModShift) s += "Shift+";
  if (c.mods & kModMeta) s += "Meta+";
  s += keyChar(c.key);
  return s;
}

uint8_t nextChordKey(uint8_t key, int step) {
  constexpr int kFirst = 0x04, kCount = 0x27 - 0x04 + 1;
  int i = isAllowedKey(key) ? key - kFirst : 0;
  i = ((i + step) % kCount + kCount) % kCount;
  return static_cast<uint8_t>(kFirst + i);
}

}  // namespace mirror
