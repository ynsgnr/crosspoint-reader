#pragma once
// Positions a ChatState as paint items using the Gfx font metrics.
#include <cstdint>
#include <string>
#include <vector>

#include "core/Gfx.h"
#include "templates/chat/chat_gen.h"

namespace mirror::chat {

struct Item {
  enum class Kind : uint8_t { Text, Rule, Bar } kind = Kind::Text;
  Rect r;
  Font font = Font::Body;
  std::string text;
  bool black = true;
  uint64_t key() const;  // identical key = identical pixels
};

struct LayoutMetrics {
  int margin = 8;
  int barWidth = 3;
  int draftLines = 3;  // fixed: typing never moves the message list
};

// Word wrap by pixel width. Hard-breaks words longer than the line. "\n" starts a new line.
std::vector<std::string> wrapText(const Gfx& g, Font f, const std::string& text, int maxWidth);

// "HH:MM" of a timestamp that is already in the phone's local time.
std::string formatTime(uint32_t localEpochSeconds);

std::vector<Item> layout(const Gfx& g, const gen::chat::ChatState& s, const LayoutMetrics& m = {});

}  // namespace mirror::chat
