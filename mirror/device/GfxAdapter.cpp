#if defined(CROSSPOINT_MIRROR)
#include "device/GfxAdapter.h"

#include "fontIds.h"

namespace mirror {
namespace {
int fontId(Font f) {
  switch (f) {
    case Font::Small:
      return SMALL_FONT_ID;
    case Font::Title:
      return UI_12_FONT_ID;
    default:
      return UI_10_FONT_ID;
  }
}
EpdFontFamily::Style styleOf(Font f) { return f == Font::Title ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR; }
}  // namespace

int GfxAdapter::textWidth(Font f, const std::string& s) const {
  return r_.getTextWidth(fontId(f), s.c_str(), styleOf(f));
}
int GfxAdapter::lineHeight(Font f) const { return r_.getLineHeight(fontId(f)); }
void GfxAdapter::drawText(Font f, int x, int y, const std::string& s, bool black) {
  r_.drawText(fontId(f), x, y, s.c_str(), black, styleOf(f));
}

std::vector<FontInfo> GfxAdapter::fonts() const {
  // Point sizes of the UI fonts registered in main.cpp.
  return {{"body", 10, lineHeight(Font::Body)},
          {"small", 8, lineHeight(Font::Small)},
          {"title", 12, lineHeight(Font::Title)}};
}

}  // namespace mirror
#endif
