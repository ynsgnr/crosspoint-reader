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

std::vector<LineRange> wrapRanges(const Gfx& g, Font f, const std::string& text, int maxWidth) {
  std::vector<LineRange> lines;
  size_t para = 0;
  while (true) {
    const size_t nl = text.find('\n', para);
    const size_t paraEnd = nl == std::string::npos ? text.size() : nl;
    size_t lineStart = para, lineEnd = para, i = para;
    bool placed = false;  // a word already sits on the current line
    while (true) {
      size_t wordEnd = text.find(' ', i);
      if (wordEnd == std::string::npos || wordEnd > paraEnd) wordEnd = paraEnd;
      if (g.textWidth(f, text.substr(lineStart, wordEnd - lineStart)) <= maxWidth) {
        lineEnd = wordEnd;
        placed = true;
      } else {
        if (placed) {
          lines.push_back({lineStart, lineEnd});
          lineStart = i;
        }
        // The word alone on a line; hard-break it while it is still too wide.
        while (wordEnd > lineStart && g.textWidth(f, text.substr(lineStart, wordEnd - lineStart)) > maxWidth) {
          size_t cut = lineStart, next = lineStart;
          while (next < wordEnd) {
            next = std::min(wordEnd,
                            next + utf8::seqLen(static_cast<uint8_t>(text[next])));  // a cut lead byte stays inside
            if (g.textWidth(f, text.substr(lineStart, next - lineStart)) > maxWidth) break;
            cut = next;
          }
          if (cut == lineStart)
            cut = std::min(wordEnd, lineStart + utf8::seqLen(static_cast<uint8_t>(text[lineStart])));
          lines.push_back({lineStart, cut});
          lineStart = cut;
        }
        lineEnd = wordEnd;
        placed = true;
      }
      if (wordEnd >= paraEnd) break;
      i = wordEnd + 1;
    }
    lines.push_back({lineStart, lineEnd});
    if (nl == std::string::npos) break;
    para = nl + 1;
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

// Rule, the draft box and the cursor. The box shows `lines` from `first`: the end of the text, or the cursor's line
// (with one line above it) when the cursor sits further up.
void layoutDraft(const Gfx& g, const std::vector<LineRange>& lines, size_t first, size_t shown, size_t cursor,
                 const std::string& draft, const LayoutMetrics& m, int top, std::vector<Item>& out) {
  const int W = g.width(), bodyH = g.lineHeight(Font::Body);
  out.push_back(shape(Item::Kind::Rule, {0, top, W, 2}));
  const int promptW = g.textWidth(Font::Body, "> ");
  const int textW = W - 2 * m.margin - promptW;
  int y = top + 5;
  for (size_t i = first; i < first + shown && i < lines.size(); ++i, y += bodyH) {
    if (i == first) out.push_back(textItem({m.margin, y, promptW, bodyH}, Font::Body, first == 0 ? ">" : kEllipsis));
    out.push_back(textItem({m.margin + promptW, y, textW, bodyH}, Font::Body,
                           draft.substr(lines[i].begin, lines[i].end - lines[i].begin)));
  }
  // The cursor: on the last line that starts at or before its offset.
  size_t ci = 0;
  for (size_t i = 0; i < lines.size(); ++i)
    if (lines[i].begin <= cursor) ci = i;
  const size_t col = std::min(cursor, lines[ci].end) - lines[ci].begin;
  const int cursorX = m.margin + promptW + g.textWidth(Font::Body, draft.substr(lines[ci].begin, col)) + 2;
  const int cursorY = top + 5 + static_cast<int>(ci - first) * bodyH + 2;
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
  const int bodyH = g.lineHeight(Font::Body);
  // The draft box grows with the text; it leaves room for one message (its meta line and one text line).
  const int room = g.height() - headerBottom - 12 - (g.lineHeight(Font::Small) + bodyH + 14);
  const size_t maxShown = static_cast<size_t>(std::max(m.draftLines, room / bodyH));
  const int promptW = g.textWidth(Font::Body, "> ");
  const auto lines = wrapRanges(g, Font::Body, s.draft, g.width() - 2 * m.margin - promptW - 4);
  const size_t shown = std::clamp(lines.size(), static_cast<size_t>(m.draftLines), maxShown);
  const size_t cursor = std::min<size_t>(s.draft_cursor, s.draft.size());
  size_t cursorLine = 0;
  for (size_t i = 0; i < lines.size(); ++i)
    if (lines[i].begin <= cursor) cursorLine = i;
  size_t first = lines.size() > shown ? lines.size() - shown : 0;
  if (cursorLine < first) first = cursorLine > 0 ? cursorLine - 1 : 0;
  first = std::min(first, lines.size() > shown ? lines.size() - shown : 0);
  const int draftTop = g.height() - static_cast<int>(shown) * bodyH - 12;
  layoutDraft(g, lines, first, shown, cursor, s.draft, m, draftTop, items);
  layoutBody(g, s, m, headerBottom + 4, draftTop - 4, items);
  return items;
}

}  // namespace mirror::chat
