#include "templates/list/ListLayout.h"

#include <algorithm>

#include "core/Utf8.h"

namespace mirror::list {

using gen::list::ListState;

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

namespace {
int headerHeight(const Gfx& g) { return g.lineHeight(Font::Title) + 12; }

Item text(const Rect& r, Font f, std::string t, bool black) {
  Item i;
  i.kind = Item::Kind::Text;
  i.r = r;
  i.font = f;
  i.text = std::move(t);
  i.black = black;
  return i;
}

Item shape(Item::Kind k, const Rect& r) {
  Item i;
  i.kind = k;
  i.r = r;
  return i;
}
}  // namespace

int rowHeight(const Gfx& g, const LayoutMetrics& m) {
  return g.lineHeight(Font::Body) + g.lineHeight(Font::Small) + 2 * m.padding + 1;
}

size_t visibleRows(const Gfx& g, const LayoutMetrics& m) {
  return static_cast<size_t>(std::max(1, (g.height() - headerHeight(g)) / rowHeight(g, m)));
}

size_t keepVisible(const Gfx& g, const ListState& s, size_t first, const LayoutMetrics& m) {
  const size_t n = s.items.size(), vis = visibleRows(g, m);
  if (n <= vis) return 0;
  size_t sel = n;
  for (size_t i = 0; i < n; ++i)
    if (s.items[i].id == s.selected) sel = i;
  if (sel < n) {
    if (sel < first) first = sel;
    if (sel >= first + vis) first = sel + 1 - vis;
  }
  return std::min(first, n - vis);
}

std::string fit(const Gfx& g, Font f, const std::string& t, int maxWidth) {
  if (g.textWidth(f, t) <= maxWidth) return t;
  size_t end = t.size();
  while (end > 0) {
    --end;
    while (end > 0 && !utf8::isBoundary(t, end)) --end;
    const std::string cut = t.substr(0, end) + "...";
    if (g.textWidth(f, cut) <= maxWidth) return cut;
  }
  return "...";
}

Layout layout(const Gfx& g, const ListState& s, size_t first, const LayoutMetrics& m) {
  Layout out;
  const int W = g.width(), titleH = g.lineHeight(Font::Title), bodyH = g.lineHeight(Font::Body),
            smallH = g.lineHeight(Font::Small), rowH = rowHeight(g, m), top = headerHeight(g);

  // Header: title, "first-last/total" when the list does not fit, rule.
  int right = W - m.margin;
  const size_t n = s.items.size(), vis = visibleRows(g, m);
  first = std::min(first, n > vis ? n - vis : 0);
  if (n > vis) {
    const std::string pos =
        std::to_string(first + 1) + "-" + std::to_string(std::min(n, first + vis)) + "/" + std::to_string(n);
    const int w = g.textWidth(Font::Small, pos);
    right -= w;
    out.items.push_back(text({right, 4 + (titleH - smallH) / 2, w, smallH}, Font::Small, pos, true));
    right -= 8;
  }
  out.items.push_back(text({m.margin, 4, std::max(0, right - m.margin), titleH}, Font::Title,
                           fit(g, Font::Title, s.title, std::max(0, right - m.margin)), true));
  out.items.push_back(shape(Item::Kind::Rule, {0, top - 3, W, 2}));

  for (size_t i = first; i < n && i < first + vis; ++i) {
    const auto& it = s.items[i];
    const int y = top + static_cast<int>(i - first) * rowH;
    const bool sel = it.id == s.selected;
    const bool ink = !sel;  // the selected row is inverted: white text on a black row
    out.rows.push_back({{0, y, W, rowH}, static_cast<uint32_t>(it.id)});
    if (sel) out.items.push_back(shape(Item::Kind::Fill, {0, y, W, rowH - 1}));
    int metaW = it.meta.empty() ? 0 : g.textWidth(Font::Small, it.meta);
    if (metaW)
      out.items.push_back(
          text({W - m.margin - metaW, y + m.padding + (bodyH - smallH) / 2, metaW, smallH}, Font::Small, it.meta, ink));
    const int labelW = W - 2 * m.margin - (metaW ? metaW + 8 : 0);
    out.items.push_back(
        text({m.margin, y + m.padding, labelW, bodyH}, Font::Body, fit(g, Font::Body, it.label, labelW), ink));
    int detailW = W - 2 * m.margin;
    if (it.badge) {
      const std::string b = it.badge > 99 ? "99+" : std::to_string(it.badge);
      const int bw = g.textWidth(Font::Small, b) + 10;
      const Rect box{W - m.margin - bw, y + m.padding + bodyH, bw, smallH};
      out.items.push_back(shape(Item::Kind::Fill, box));
      out.items.back().black = !sel;  // a black box, white on the selected row
      out.items.push_back(text({box.x + 5, box.y, bw - 10, smallH}, Font::Small, b, sel));
      detailW -= bw + 8;
    }
    out.items.push_back(text({m.margin, y + m.padding + bodyH, detailW, smallH}, Font::Small,
                             fit(g, Font::Small, it.detail, detailW), ink));
    if (!sel) out.items.push_back(shape(Item::Kind::Rule, {m.margin, y + rowH - 1, W - 2 * m.margin, 1}));
  }
  return out;
}

}  // namespace mirror::list
