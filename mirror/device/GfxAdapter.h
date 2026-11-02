#pragma once
#if defined(CROSSPOINT_MIRROR)
// mirror::Gfx over the firmware's GfxRenderer. Uses the logical (oriented) coordinate space.
#include <GfxRenderer.h>

#include "core/Gfx.h"

namespace mirror {

class GfxAdapter final : public Gfx {
 public:
  explicit GfxAdapter(GfxRenderer& r) : r_(r) {}
  int width() const override { return r_.getScreenWidth(); }
  int height() const override { return r_.getScreenHeight(); }
  int textWidth(Font f, const std::string& s) const override;
  int lineHeight(Font f) const override;
  void clear() override { r_.clearScreen(); }
  void fillRect(int x, int y, int w, int h, bool black) override { r_.fillRect(x, y, w, h, black); }
  void drawText(Font f, int x, int y, const std::string& s, bool black) override;
  std::vector<FontInfo> fonts() const override;

 private:
  GfxRenderer& r_;
};

}  // namespace mirror
#endif
