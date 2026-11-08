#include "core/Frame.h"

#include "core/BandedGfx.h"

namespace mirror {

int statusBandHeight(const Gfx& gfx) { return gfx.lineHeight(Font::Small) + 4; }

namespace {

// A disc of radius r, from horizontal runs (Gfx has rectangles only).
void fillDisc(Gfx& gfx, int cx, int cy, int r, bool black) {
  for (int dy = -r; dy <= r; ++dy) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) ++dx;
    gfx.fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, black);
  }
}

}  // namespace

bool bandDarkMode(const std::optional<gen::PhoneStatus>& phone) { return phone && phone->dark_mode; }

DirtyRects renderFrame(TemplateRenderer& renderer, Gfx& gfx, bool darkMode) {
  const int band = statusBandHeight(gfx);
  BandedGfx framed(gfx, band);
  const DirtyRects dirty = renderer.render(framed);
  const int r = (band - 6) / 2, cx = 8 + r, cy = band / 2;
  fillDisc(gfx, cx, cy, r, true);                      // the whole dot, or its ring
  if (!darkMode) fillDisc(gfx, cx, cy, r - 2, false);  // hollow while dark mode is off
  const std::string hint = "Back: menu";
  gfx.drawText(Font::Small, gfx.width() - 8 - gfx.textWidth(Font::Small, hint), 2, hint, true);
  return dirty;
}

}  // namespace mirror
