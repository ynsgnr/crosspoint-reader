#include "core/Frame.h"

#include "core/BandedGfx.h"

namespace mirror {

int statusBandHeight(const Gfx& gfx) { return gfx.lineHeight(Font::Small) + 4; }

std::string statusBandText(const std::optional<gen::PhoneStatus>& phone) {
  if (phone && phone->dark_mode) return "Phone dark mode on: tap here to end    Back: menu";
  return "Tap here: phone dark mode    Back: menu";
}

DirtyRects renderFrame(TemplateRenderer& renderer, Gfx& gfx, const std::string& bandText) {
  BandedGfx framed(gfx, statusBandHeight(gfx));
  const DirtyRects dirty = renderer.render(framed);
  gfx.drawText(Font::Small, 8, 2, bandText, true);
  return dirty;
}

}  // namespace mirror
