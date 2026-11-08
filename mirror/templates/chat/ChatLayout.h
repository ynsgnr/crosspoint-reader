#pragma once
// Positions a ChatState as paint items using the Gfx font metrics.
#include <cstdint>
#include <string>
#include <vector>

#include "core/Gfx.h"
#include "templates/chat/chat_gen.h"

namespace mirror::chat {

struct Item {
  // ArrowDown "\/" hangs from the top of its rect, ArrowUp "/\" stands on its bottom: one pixel per row.
  enum class Kind : uint8_t { Text, Rule, Bar, ArrowDown, ArrowUp } kind = Kind::Text;
  Rect r;
  Font font = Font::Body;
  std::string text;
  bool black = true;
  uint64_t key() const;  // identical key = identical pixels
};

struct LayoutMetrics {
  int margin = 8;
  int barWidth = 4;
  int draftLines = 3;  // the draft box is at least this tall and grows with the text, up to most of the screen
};

// A wrapped line as a byte range of the original text (a trailing space or newline is not part of it).
struct LineRange {
  size_t begin = 0, end = 0;
};

// Like wrapText, but keeps where each line came from so a cursor offset can be placed. Never empty.
std::vector<LineRange> wrapRanges(const Gfx& g, Font f, const std::string& text, int maxWidth);

// Word wrap by pixel width. Hard-breaks words longer than the line. "\n" starts a new line.
std::vector<std::string> wrapText(const Gfx& g, Font f, const std::string& text, int maxWidth);

// "HH:MM" of a timestamp that is already in the phone's local time.
std::string formatTime(uint32_t localEpochSeconds);

// A message with this text (U+FFFC, object replacement character) is an image or other non-text item on the phone.
constexpr const char* kMediaText = "\xEF\xBF\xBC";

std::vector<Item> layout(const Gfx& g, const gen::chat::ChatState& s, const LayoutMetrics& m = {});

}  // namespace mirror::chat
