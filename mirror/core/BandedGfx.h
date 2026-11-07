#pragma once
// A Gfx that leaves the top `band` pixels to the caller: the template draws in the rest, shifted down.
#include "core/Gfx.h"

namespace mirror {

class BandedGfx final : public Gfx {
 public:
  BandedGfx(Gfx& inner, int band) : inner_(inner), band_(band) {}
  int width() const override { return inner_.width(); }
  int height() const override { return inner_.height() - band_; }
  int textWidth(Font f, const std::string& s) const override { return inner_.textWidth(f, s); }
  int lineHeight(Font f) const override { return inner_.lineHeight(f); }
  void clear() override { inner_.clear(); }  // the whole frame, band included
  void fillRect(int x, int y, int w, int h, bool black) override { inner_.fillRect(x, y + band_, w, h, black); }
  void drawText(Font f, int x, int y, const std::string& s, bool black) override {
    inner_.drawText(f, x, y + band_, s, black);
  }
  std::vector<FontInfo> fonts() const override { return inner_.fonts(); }

 private:
  Gfx& inner_;
  int band_;
};

}  // namespace mirror
