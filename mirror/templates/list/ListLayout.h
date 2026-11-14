#pragma once
// Positions a ListState as paint items using the Gfx font metrics.
#include <cstdint>
#include <string>
#include <vector>

#include "core/Gfx.h"
#include "templates/list/list_gen.h"

namespace mirror::list {

struct Item {
  enum class Kind : uint8_t { Text, Fill, Rule } kind = Kind::Text;
  Rect r;
  Font font = Font::Body;
  std::string text;
  bool black = true;
  uint64_t key() const;  // identical key = identical pixels
};

// Where a row sits on the frame (the Gfx the template draws in), for touch hit tests.
struct RowRect {
  Rect r;
  uint32_t id = 0;
};

struct Layout {
  std::vector<Item> items;
  std::vector<RowRect> rows;  // the visible rows only
};

struct LayoutMetrics {
  int margin = 8;
  int padding = 6;  // above and below the text of a row
};

// The smallest height of one row, and how many of those fit under the title.
int rowHeight(const Gfx& g, const LayoutMetrics& m = {});
size_t visibleRows(const Gfx& g, const LayoutMetrics& m = {});

// The height each of `itemCount` rows gets: the rows share the whole height under the title (no empty band below them),
// within [rowHeight, 4 * rowHeight] so a short list does not turn into a few huge rows.
int rowHeightFor(const Gfx& g, size_t itemCount, const LayoutMetrics& m = {});

// Index of the first row to show so that the selected row (if any) is visible, starting from `first`.
size_t keepVisible(const Gfx& g, const gen::list::ListState& s, size_t first, const LayoutMetrics& m = {});

// Cuts `text` at a code point boundary so it fits maxWidth, ending in "..." when it was cut.
std::string fit(const Gfx& g, Font f, const std::string& text, int maxWidth);

// `first` is the index of the first visible row (see keepVisible).
Layout layout(const Gfx& g, const gen::list::ListState& s, size_t first, const LayoutMetrics& m = {});

}  // namespace mirror::list
