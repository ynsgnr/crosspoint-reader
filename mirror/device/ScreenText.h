#pragma once
#if defined(CROSSPOINT_MIRROR)
// Centered lines of text stacked down the screen.
#include <GfxRenderer.h>

#include <string>

#include "fontIds.h"

namespace mirror {

class TextColumn {
 public:
  TextColumn(const GfxRenderer& r, int startY) : r_(r), y_(startY), step_(r.getLineHeight(UI_10_FONT_ID) + 6) {}

  void line(const std::string& text, bool bold = false) {
    r_.drawCenteredText(UI_10_FONT_ID, y_, text.c_str(), true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y_ += step_;
  }
  void gap() { y_ += step_ / 2; }
  void moveTo(int y) { y_ = y; }
  int step() const { return step_; }

 private:
  const GfxRenderer& r_;
  int y_;
  int step_;
};

}  // namespace mirror
#endif
