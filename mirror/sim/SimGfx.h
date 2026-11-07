#pragma once
// 1-bit canvas implementing mirror::Gfx with baked DejaVu fonts. Stands in for GfxRenderer on the host.
#include <cstdint>
#include <vector>

#include "core/Gfx.h"
#include "core/RefreshPolicy.h"

namespace mirror::sim {

class SimGfx final : public Gfx {
 public:
  SimGfx(int w, int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * h, 0) {}
  int width() const override { return w_; }
  int height() const override { return h_; }
  int textWidth(Font f, const std::string& utf8) const override;
  int lineHeight(Font f) const override;
  void clear() override { std::fill(px_.begin(), px_.end(), 0); }
  void fillRect(int x, int y, int w, int h, bool black) override;
  void drawText(Font f, int x, int y, const std::string& utf8, bool black) override;
  std::vector<FontInfo> fonts() const override;

  bool black(int x, int y) const { return px_[static_cast<size_t>(y) * w_ + x] != 0; }
  // RGB image; dirty rects are outlined in red when given.
  std::vector<uint8_t> toRgb(const std::vector<Rect>& outline = {}) const;

 private:
  int w_, h_;
  std::vector<uint8_t> px_;
};

}  // namespace mirror::sim
