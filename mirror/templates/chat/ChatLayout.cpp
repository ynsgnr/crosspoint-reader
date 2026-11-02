#include "templates/chat/ChatLayout.h"

#include <algorithm>
#include <cstdio>

#include "core/Utf8.h"

namespace mirror::chat {

uint64_t Item::key() const {
  uint64_t h = 1469598103934665603ULL;
  auto mix = [&h](uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
  };
  mix(static_cast<uint64_t>(kind));
  mix(static_cast<uint64_t>(r.x) & 0xFFFF);
  mix(static_cast<uint64_t>(r.y) & 0xFFFF);
  mix(static_cast<uint64_t>(r.w) & 0xFFFF);
  mix(static_cast<uint64_t>(r.h) & 0xFFFF);
  mix(static_cast<uint64_t>(font));
  mix(black ? 1 : 0);
  for (unsigned char c : text) mix(c);
  return h;
}

std::vector<std::string> wrapText(const Gfx& g, Font f, const std::string& text, int maxWidth) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    const std::string para = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    std::string line;
    size_t i = 0;
    while (i <= para.size()) {
      size_t sp = para.find(' ', i);
      std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
      i = sp == std::string::npos ? para.size() + 1 : sp + 1;
      std::string candidate = line.empty() ? word : line + " " + word;
      if (g.textWidth(f, candidate) <= maxWidth) {
        line = std::move(candidate);
        continue;
      }
      if (!line.empty()) {
        lines.push_back(line);
        line.clear();
      }
      // Word on its own line; hard-break it if it is still too wide.
      while (g.textWidth(f, word) > maxWidth && !word.empty()) {
        size_t cut = 0, next = 0;
        while (next < word.size()) {
          next = cut + utf8::seqLen(static_cast<uint8_t>(word[cut]));
          if (g.textWidth(f, word.substr(0, next)) > maxWidth) break;
          cut = next;
        }
        if (cut == 0) cut = utf8::seqLen(static_cast<uint8_t>(word[0]));  // always make progress
        lines.push_back(word.substr(0, cut));
        word.erase(0, cut);
      }
      line = word;
    }
    lines.push_back(line);
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  return lines;
}

std::string formatTime(uint32_t t) {
  char b[8];
  std::snprintf(b, sizeof b, "%02u:%02u", (t / 3600) % 24, (t / 60) % 60);
  return b;
}

namespace {
Item shape(Item::Kind kind, Rect r) {
  Item i;
  i.kind = kind;
  i.r = r;
  return i;
}

std::string clipToWidth(const Gfx& g, Font f, std::string s, int maxWidth) {
  if (g.textWidth(f, s) <= maxWidth) return s;
  const std::string ell = "\xE2\x80\xA6";
  while (!s.empty()) {
    size_t cut = s.size() - 1;
    while (cut > 0 && !utf8::isBoundary(s, cut)) --cut;
    s.erase(cut);
    if (g.textWidth(f, s + ell) <= maxWidth) return s + ell;
  }
  return ell;
}
}  // namespace

namespace {
const char* const kEllipsis = "\xE2\x80\xA6";

Item textItem(Rect r, Font font, std::string text, bool black = true) {
  Item i;
  i.r = r;
  i.font = font;
  i.text = std::move(text);
  i.black = black;
  return i;
}

// Title, scroll marker and unread badge on one row, rule below. Returns the y of the rule's bottom edge.
int layoutHeader(const Gfx& g, const gen::chat::ChatState& s, const LayoutMetrics& m, std::vector<Item>& out) {
  const int W = g.width(), titleH = g.lineHeight(Font::Title), bodyH = g.lineHeight(Font::Body);
  int right = W - m.margin;
  if (s.unread) {
    const std::string badge = std::to_string(s.unread);
    const int badgeW = g.textWidth(Font::Body, badge) + 12;
    right -= badgeW;
    const Rect box{right, 4 + (titleH - bodyH - 4) / 2, badgeW, bodyH + 4};
    out.push_back(shape(Item::Kind::Bar, box));
    out.push_back(textItem({box.x + 6, box.y + 2, badgeW - 12, bodyH}, Font::Body, badge, false));
    right -= 8;
  }
  if (s.scroll) {  // newer messages are below the view
    const std::string marker = "v" + std::to_string(s.scroll);
    const int markerW = g.textWidth(Font::Small, marker);
    out.push_back(
        textItem({right - markerW, 4 + (titleH - g.lineHeight(Font::Small)) / 2, markerW, g.lineHeight(Font::Small)},
                 Font::Small, marker));
    right -= markerW + 8;
  }
  const int titleW = right - m.margin;
  out.push_back(textItem({m.margin, 4, titleW, titleH}, Font::Title, clipToWidth(g, Font::Title, s.title, titleW)));
  const int bottom = 4 + titleH + 4;
  out.push_back(shape(Item::Kind::Rule, {0, bottom, W, 2}));
  return bottom;
}

// Rule, a fixed-height draft area showing the last lines, and the cursor. Fixed height: typing never moves the list.
void layoutDraft(const Gfx& g, const gen::chat::ChatState& s, const LayoutMetrics& m, int top, std::vector<Item>& out) {
  const int W = g.width(), bodyH = g.lineHeight(Font::Body);
  out.push_back(shape(Item::Kind::Rule, {0, top, W, 2}));
  const int promptW = g.textWidth(Font::Body, "> ");
  const int textW = W - 2 * m.margin - promptW;
  const auto lines = wrapText(g, Font::Body, s.draft, textW - 4);  // never empty: "" wraps to one empty line
  const size_t first = lines.size() > static_cast<size_t>(m.draftLines) ? lines.size() - m.draftLines : 0;
  int y = top + 5;
  for (size_t i = first; i < lines.size(); ++i, y += bodyH) {
    if (i == first) out.push_back(textItem({m.margin, y, promptW, bodyH}, Font::Body, first == 0 ? ">" : kEllipsis));
    out.push_back(textItem({m.margin + promptW, y, textW, bodyH}, Font::Body, lines[i]));
  }
  const int cursorX = m.margin + promptW + g.textWidth(Font::Body, lines.back()) + 2;
  const int cursorY = top + 5 + static_cast<int>(lines.size() - 1 - first) * bodyH + 2;
  out.push_back(shape(Item::Kind::Bar, {std::min(cursorX, W - m.margin - 2), cursorY, 2, bodyH - 4}));
}

// Newest message at the bottom, skipping `scroll` newest ones, filling upwards. A message that does not fit
// shows its last lines under a line holding only an ellipsis.
void layoutBody(const Gfx& g, const gen::chat::ChatState& s, const LayoutMetrics& m, int top, int bottom,
                std::vector<Item>& out) {
  const int W = g.width(), bodyH = g.lineHeight(Font::Body), smallH = g.lineHeight(Font::Small);
  const int indent = m.barWidth + 6, textW = W - 2 * m.margin - indent;
  if (s.messages.empty()) {
    out.push_back(textItem({m.margin, bottom - smallH, textW, smallH}, Font::Small, "No messages yet"));
    return;
  }
  const size_t skip = std::min<size_t>(s.scroll, s.messages.size() - 1);
  for (size_t idx = s.messages.size() - skip; idx-- > 0;) {
    const auto& msg = s.messages[idx];
    const auto lines = wrapText(g, Font::Body, msg.text, textW);
    const std::string meta = clipToWidth(
        g, Font::Small, (msg.outgoing ? std::string("Me") : msg.sender) + "  " + formatTime(msg.timestamp), textW);
    int blockTop = bottom - (smallH + static_cast<int>(lines.size()) * bodyH + 6);
    size_t firstLine = 0;
    bool cut = false;
    if (blockTop < top) {
      const int fit = (bottom - top - smallH - 6) / bodyH;
      if (fit < 2) break;
      firstLine = lines.size() - static_cast<size_t>(fit - 1);
      cut = true;
      blockTop = top;
    }
    std::vector<std::string> shown;
    if (cut) shown.push_back(kEllipsis);
    shown.insert(shown.end(), lines.begin() + static_cast<std::ptrdiff_t>(firstLine), lines.end());

    // Outgoing blocks are right-aligned as a whole; lines stay left-aligned inside the block.
    int blockW = g.textWidth(Font::Small, meta);
    for (const auto& l : shown) blockW = std::max(blockW, g.textWidth(Font::Body, l));
    const int x = msg.outgoing ? W - m.margin - indent - blockW : m.margin + indent;
    out.push_back(textItem({x, blockTop, blockW, smallH}, Font::Small, meta));
    int y = blockTop + smallH;
    for (const auto& l : shown) {
      out.push_back(textItem({x, y, blockW, bodyH}, Font::Body, l));
      y += bodyH;
    }
    const Rect bar = msg.outgoing ? Rect{W - m.margin - m.barWidth, blockTop, m.barWidth, y - blockTop}
                                  : Rect{m.margin, blockTop, 2, y - blockTop};
    out.push_back(shape(Item::Kind::Bar, bar));
    bottom = blockTop - 4;
    if (cut || bottom <= top) break;
  }
}
}  // namespace

std::vector<Item> layout(const Gfx& g, const gen::chat::ChatState& s, const LayoutMetrics& m) {
  std::vector<Item> items;
  const int headerBottom = layoutHeader(g, s, m, items);
  const int draftTop = g.height() - m.draftLines * g.lineHeight(Font::Body) - 12;
  layoutDraft(g, s, m, draftTop, items);
  layoutBody(g, s, m, headerBottom + 4, draftTop - 4, items);
  return items;
}

}  // namespace mirror::chat
