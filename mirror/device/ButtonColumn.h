#pragma once
#if defined(CROSSPOINT_MIRROR)
// A column of full-width buttons. Touch boards tap them; boards without touch move a cursor and press Confirm.
#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "fontIds.h"

namespace mirror {

class ButtonColumn {
 public:
  void clear() {
    labels_.clear();
    ids_.clear();
  }
  void add(std::string label, int id) {
    labels_.push_back(std::move(label));
    ids_.push_back(id);
  }
  int count() const { return static_cast<int>(ids_.size()); }
  int id(int index) const { return ids_[index]; }

  // Index of the button at (x, y), or -1.
  int hit(const GfxRenderer& r, int x, int y, int top) const {
    for (int i = 0; i < count(); ++i) {
      const Box b = bounds(r, i, top);
      if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
    }
    return -1;
  }

  // The cursor is drawn only where there is no touch: e-ink would keep showing the last selection.
  void draw(const GfxRenderer& r, int top, int cursor, bool showCursor) const {
    for (int i = 0; i < count(); ++i) {
      const Box b = bounds(r, i, top);
      const bool selected = showCursor && i == cursor;
      r.drawRect(b.x, b.y, b.w, b.h, selected ? 4 : 1, true);
      const int textWidth = r.getTextWidth(UI_10_FONT_ID, labels_[i].c_str());
      r.drawText(UI_10_FONT_ID, b.x + (b.w - textWidth) / 2, b.y + (b.h - r.getLineHeight(UI_10_FONT_ID)) / 2,
                 labels_[i].c_str(), true, selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
  }

  // Bottom edge of the last button.
  int bottom(const GfxRenderer& r, int top) const {
    return count() ? bounds(r, count() - 1, top).y + rowHeight(r) : top;
  }

 private:
  struct Box {
    int x, y, w, h;
  };
  static int rowHeight(const GfxRenderer& r) { return r.getLineHeight(UI_10_FONT_ID) * 2 + 8; }  // finger sized
  static Box bounds(const GfxRenderer& r, int i, int top) {
    constexpr int kMargin = 24, kGap = 10;
    return Box{kMargin, top + i * (rowHeight(r) + kGap), r.getScreenWidth() - 2 * kMargin, rowHeight(r)};
  }

  std::vector<std::string> labels_;
  std::vector<int> ids_;
};

}  // namespace mirror
#endif
