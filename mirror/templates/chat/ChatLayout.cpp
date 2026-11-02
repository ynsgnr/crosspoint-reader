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

std::vector<Item> layout(const Gfx& g, const gen::chat::ChatState& s, const LayoutMetrics& m) {
  std::vector<Item> items;
  const int W = g.width(), H = g.height();
  const int titleH = g.lineHeight(Font::Title), bodyH = g.lineHeight(Font::Body), smallH = g.lineHeight(Font::Small);
  const int contentW = W - 2 * m.margin;

  // Header: title left, unread badge right, rule below.
  std::string badge = s.unread ? std::to_string(s.unread) : "";
  const int badgeW = badge.empty() ? 0 : g.textWidth(Font::Body, badge) + 12;
  Item t;
  t.r = {m.margin, 4, contentW - badgeW, titleH};
  t.font = Font::Title;
  t.text = clipToWidth(g, Font::Title, s.title, t.r.w);
  items.push_back(t);
  if (!badge.empty()) {
    Item bg;
    bg.kind = Item::Kind::Bar;
    bg.r = {W - m.margin - badgeW, 4 + (titleH - bodyH - 4) / 2, badgeW, bodyH + 4};
    items.push_back(bg);
    Item bt;
    bt.r = {bg.r.x + 6, bg.r.y + 2, badgeW - 12, bodyH};
    bt.text = badge;
    bt.black = false;
    items.push_back(bt);
  }
  const int headerBottom = 4 + titleH + 4;
  items.push_back(shape(Item::Kind::Rule, {0, headerBottom, W, 2}));

  // Footer: rule + fixed draft area (last N wrapped lines) + cursor.
  const int draftH = m.draftLines * bodyH + 6;
  const int footerTop = H - draftH - 6;
  items.push_back(shape(Item::Kind::Rule, {0, footerTop, W, 2}));
  {
    const int promptW = g.textWidth(Font::Body, "> ");
    auto lines = wrapText(g, Font::Body, s.draft, contentW - promptW - 4);
    const size_t first = lines.size() > static_cast<size_t>(m.draftLines) ? lines.size() - m.draftLines : 0;
    int y = footerTop + 5;
    for (size_t i = first; i < lines.size(); ++i, y += bodyH) {
      Item d;
      d.r = {m.margin + promptW, y, contentW - promptW, bodyH};
      d.text = lines[i];
      items.push_back(d);
      if (i == first) {
        Item p;
        p.r = {m.margin, y, promptW, bodyH};
        p.text = first == 0 ? ">" : "\xE2\x80\xA6";
        items.push_back(p);
      }
    }
    if (lines.empty() || (lines.size() == 1 && lines[0].empty())) {
      Item p;
      p.r = {m.margin, footerTop + 5, promptW, bodyH};
      p.text = ">";
      items.push_back(p);
    }
    const size_t lastIdx = lines.empty() ? 0 : lines.size() - 1;
    const int cy = footerTop + 5 + static_cast<int>(lastIdx - std::min(lastIdx, first)) * bodyH;
    const int cx = m.margin + promptW + (lines.empty() ? 0 : g.textWidth(Font::Body, lines.back())) + 2;
    items.push_back(shape(Item::Kind::Bar, {std::min(cx, W - m.margin - 2), cy + 2, 2, bodyH - 4}));
  }

  // Body: newest message at the bottom, skipping `scroll` newest ones; fill upwards.
  const int bodyTop = headerBottom + 4, bodyBottom = footerTop - 4;
  int cursorBottom = bodyBottom;
  const size_t total = s.messages.size();
  const size_t skip = std::min<size_t>(s.scroll, total ? total - 1 : 0);
  for (size_t idx = total - skip; idx-- > 0;) {
    const auto& msg = s.messages[idx];
    const int indent = m.barWidth + 6;
    const int textW = contentW - indent;
    auto lines = wrapText(g, Font::Body, msg.text, textW);
    const int blockH = smallH + static_cast<int>(lines.size()) * bodyH + 6;
    int top = cursorBottom - blockH;
    size_t dropLines = 0;
    if (top < bodyTop) {  // partly visible: keep the last lines that fit, mark the cut
      const int fit = (cursorBottom - bodyTop - smallH - 6) / bodyH;
      if (fit < 1) break;
      dropLines = lines.size() - static_cast<size_t>(fit);
      top = bodyTop;
    }
    const int x0 = msg.outgoing ? m.margin : m.margin + indent;
    std::string meta = (msg.outgoing ? std::string("Me") : msg.sender) + "  " + formatTime(msg.timestamp);
    Item mi;
    mi.r = {x0, top, textW, smallH};
    mi.font = Font::Small;
    mi.text = clipToWidth(g, Font::Small, meta, textW);
    if (msg.outgoing) mi.r.x = W - m.margin - indent - g.textWidth(Font::Small, mi.text);
    items.push_back(mi);
    int y = top + smallH;
    for (size_t li = dropLines; li < lines.size(); ++li, y += bodyH) {
      Item l;
      l.r = {x0, y, textW, bodyH};
      l.text = (li == dropLines && dropLines > 0) ? "\xE2\x80\xA6 " + lines[li] : lines[li];
      if (msg.outgoing) l.r.x = W - m.margin - indent - g.textWidth(Font::Body, l.text);
      items.push_back(l);
    }
    // Direction bar: outgoing = solid bar on the right, incoming = thin rule on the left.
    Item bar;
    bar.kind = Item::Kind::Bar;
    bar.r = msg.outgoing ? Rect{W - m.margin - m.barWidth, top, m.barWidth, y - top} : Rect{m.margin, top, 1, y - top};
    items.push_back(bar);
    cursorBottom = top - 4;
    if (dropLines > 0 || cursorBottom <= bodyTop) break;
  }
  return items;
}

}  // namespace mirror::chat
