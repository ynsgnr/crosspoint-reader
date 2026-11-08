#pragma once
// Macro deck: the fixed set of shortcuts the Macros screen sends over the HID keyboard. Portable and unit tested.
#include <cstdint>

namespace macro {

struct Macro {
  const char* label;
  uint8_t mods;       // keyboard modifier byte (1 Ctrl, 2 Shift, 4 Alt, 8 Meta); 0 for consumer macros
  uint8_t usage;      // keyboard usage id; 0 when `consumer` is used
  uint16_t consumer;  // consumer-control usage (media keys); 0 when `usage` is used
};

constexpr int kMacroCols = 3;  // the grid is drawn with the same cells as the PIN pad

// Edit this table to change the deck; it must fit the 3x3 grid. Consumer usages: HID Usage Tables, page 0x0C.
constexpr Macro kMacros[] = {
    {"Play/Pause", 0, 0, 0x00CD}, {"Prev", 0, 0, 0x00B6},  {"Next", 0, 0, 0x00B5},
    {"Vol -", 0, 0, 0x00EA},      {"Vol +", 0, 0, 0x00E9}, {"Mute", 0, 0, 0x00E2},
    {"Copy", 1, 0x06, 0},         {"Paste", 1, 0x19, 0},   {"Undo", 1, 0x1D, 0},
};
constexpr int kMacroCount = static_cast<int>(sizeof(kMacros) / sizeof(kMacros[0]));
constexpr int kMacroRows = (kMacroCount + kMacroCols - 1) / kMacroCols;

}  // namespace macro
