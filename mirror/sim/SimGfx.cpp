#include "sim/SimGfx.h"

#include <algorithm>

#include "core/Utf8.h"
#include "sim/SimFontData.h"

namespace mirror::sim {
namespace {
const SimFont& fontOf(Font f) {
  switch (f) {
    case Font::Small:
      return kSmallFont;
    case Font::Title:
      return kTitleFont;
    default:
      return kBodyFont;
  }
}
uint32_t nextCp(const std::string& s, size_t& i) {
  const uint8_t b = static_cast<uint8_t>(s[i]);
  const size_t n = utf8::seqLen(b);
  uint32_t cp = n == 1 ? b : (n == 2 ? b & 0x1F : n == 3 ? b & 0x0F : b & 0x07);
  if (n > 1 && i + n <= s.size())
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
  else if (n > 1)
    cp = '?';
  i += std::min(n, s.size() - i);
  return cp;
}
const SimGlyph* find(const SimFont& f, uint32_t cp) {
  for (size_t i = 0; i < f.count; ++i)
    if (f.glyphs[i].cp == cp) return &f.glyphs[i];
  return nullptr;
}
const SimGlyph* glyph(const SimFont& f, uint32_t cp) {
  if (const SimGlyph* g = find(f, cp)) return g;
  return find(f, '?');
}
}  // namespace

int SimGfx::textWidth(Font f, const std::string& s) const {
  const SimFont& sf = fontOf(f);
  int w = 0;
  for (size_t i = 0; i < s.size();) w += glyph(sf, nextCp(s, i))->adv;
  return w;
}

int SimGfx::lineHeight(Font f) const { return fontOf(f).lineHeight; }

void SimGfx::fillRect(int x, int y, int w, int h, bool black) {
  for (int yy = std::max(0, y); yy < std::min(h_, y + h); ++yy)
    for (int xx = std::max(0, x); xx < std::min(w_, x + w); ++xx) px_[static_cast<size_t>(yy) * w_ + xx] = black;
}

void SimGfx::drawText(Font f, int x, int y, const std::string& s, bool black) {
  const SimFont& sf = fontOf(f);
  int pen = x;
  for (size_t i = 0; i < s.size();) {
    const SimGlyph* g = glyph(sf, nextCp(s, i));
    const int nb = (g->w + 7) / 8;
    for (int r = 0; r < g->h; ++r)
      for (int c = 0; c < g->w; ++c)
        if (sf.bits[g->off + r * nb + c / 8] & (0x80 >> (c % 8))) {
          const int px = pen + g->bx + c, py = y + g->by + r;
          if (px >= 0 && px < w_ && py >= 0 && py < h_) px_[static_cast<size_t>(py) * w_ + px] = black;
        }
    pen += g->adv;
  }
}

std::vector<FontInfo> SimGfx::fonts() const {
  return {{"body", kBodyFont.size, kBodyFont.lineHeight},
          {"small", kSmallFont.size, kSmallFont.lineHeight},
          {"title", kTitleFont.size, kTitleFont.lineHeight}};
}

std::vector<uint8_t> SimGfx::toRgb(const std::vector<Rect>& outline) const {
  std::vector<uint8_t> rgb(static_cast<size_t>(w_) * h_ * 3);
  for (size_t i = 0; i < px_.size(); ++i) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = px_[i] ? 0 : 255;
  auto set = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    uint8_t* p = &rgb[(static_cast<size_t>(y) * w_ + x) * 3];
    p[0] = 220;
    p[1] = 0;
    p[2] = 0;
  };
  for (const Rect& r : outline) {
    for (int x = r.x; x < r.x + r.w; ++x) {
      set(x, r.y);
      set(x, r.y + r.h - 1);
    }
    for (int y = r.y; y < r.y + r.h; ++y) {
      set(r.x, y);
      set(r.x + r.w - 1, y);
    }
  }
  return rgb;
}

}  // namespace mirror::sim
