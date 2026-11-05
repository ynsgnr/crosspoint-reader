#pragma once
// Drawing surface for template renderers: GfxRenderer on the device, a bitmap canvas in the simulator.
#include <string>
#include <vector>

namespace mirror {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  bool operator==(const Rect&) const = default;
};

enum class Font { Body, Small, Title };

struct FontInfo {
  const char* name;
  int sizePx;
  int lineHeightPx;
};

class Gfx {
 public:
  virtual ~Gfx() = default;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual int textWidth(Font f, const std::string& utf8) const = 0;
  virtual int lineHeight(Font f) const = 0;
  virtual void clear() = 0;  // white
  virtual void fillRect(int x, int y, int w, int h, bool black) = 0;
  // y is the top of the text line box (not the baseline).
  virtual void drawText(Font f, int x, int y, const std::string& utf8, bool black) = 0;
  virtual std::vector<FontInfo> fonts() const = 0;
};

}  // namespace mirror
