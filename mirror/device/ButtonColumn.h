#pragma once
#if defined(CROSSPOINT_MIRROR)
// A column of buttons: full width, or two half-width ones side by side (the app launcher). Touch boards tap them;
// boards without touch move a cursor through them in order and press Confirm.
#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "fontIds.h"

namespace mirror {

class ButtonColumn {
 public:
  void clear() { items_.clear(); }
  // `half`: shares its row with the next half-width button.
  void add(std::string label, int id, bool half = false) { items_.push_back({std::move(label), id, half}); }
  int count() const { return static_cast<int>(items_.size()); }
  int id(int index) const { return items_[index].id; }

  // Index of the button at (x, y), or -1.
  int hit(const GfxRenderer& r, int x, int y, int top) const {
    const std::vector<Box> boxes = layout(r, top);
    for (int i = 0; i < count(); ++i) {
      const Box& b = boxes[i];
      if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
    }
    return -1;
  }

  // The cursor is drawn only where there is no touch: e-ink would keep showing the last selection.
  void draw(const GfxRenderer& r, int top, int cursor, bool showCursor) const {
    const std::vector<Box> boxes = layout(r, top);
    for (int i = 0; i < count(); ++i) {
      const Box& b = boxes[i];
      const bool selected = showCursor && i == cursor;
      r.drawRect(b.x, b.y, b.w, b.h, selected ? 4 : 1, true);
      const auto style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const std::string label = r.truncatedText(UI_10_FONT_ID, items_[i].label.c_str(), b.w - 16, style);
      const int textWidth = r.getTextWidth(UI_10_FONT_ID, label.c_str(), style);
      r.drawText(UI_10_FONT_ID, b.x + (b.w - textWidth) / 2, b.y + (b.h - r.getLineHeight(UI_10_FONT_ID)) / 2,
                 label.c_str(), true, style);
    }
  }

 private:
  struct Item {
    std::string label;
    int id;
    bool half;
  };
  struct Box {
    int x, y, w, h;
  };
  static int rowHeight(const GfxRenderer& r) { return r.getLineHeight(UI_10_FONT_ID) * 2 + 8; }  // finger sized

  std::vector<Box> layout(const GfxRenderer& r, int top) const {
    constexpr int kMargin = 24, kGap = 10;
    const int width = r.getScreenWidth() - 2 * kMargin, half = (width - kGap) / 2, h = rowHeight(r);
    std::vector<Box> boxes;
    int y = top;
    bool rightFree = false;  // the previous button was a half-width one on the left
    for (const Item& item : items_) {
      if (item.half && rightFree) {
        boxes.push_back({kMargin + half + kGap, y - h - kGap, half, h});
        rightFree = false;
        continue;
      }
      boxes.push_back({kMargin, y, item.half ? half : width, h});
      rightFree = item.half;
      y += h + kGap;
    }
    return boxes;
  }

  std::vector<Item> items_;
};

}  // namespace mirror
#endif
