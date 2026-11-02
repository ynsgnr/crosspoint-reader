#pragma once
// Turns dirty rectangles into a refresh plan.
#include <vector>

#include "core/TemplateRenderer.h"

namespace mirror {

enum class RefreshKind { None, Partial, Full };

struct RefreshPlan {
  RefreshKind kind = RefreshKind::None;
  std::vector<Rect> rects;  // merged, x/width widened to multiples of 8 (byte aligned panel windows)
};

struct RefreshConfig {
  uint16_t fullRefreshEveryN = 20;  // partial refreshes between forced full ones (ghosting control)
  int mergeGapPx = 12;              // rects closer than this are merged
};

class RefreshPolicy {
 public:
  explicit RefreshPolicy(RefreshConfig c = {}) : cfg_(c) {}
  RefreshPlan plan(const DirtyRects& dirty, int screenW, int screenH, const RefreshOverrides& ov, bool forceFull);
  void reset() { partialsSinceFull_ = 0; }
  uint16_t partialsSinceFull() const { return partialsSinceFull_; }

  static std::vector<Rect> mergeRects(std::vector<Rect> rects, int gap);
  static Rect alignToBytes(Rect r, int screenW);

 private:
  RefreshConfig cfg_;
  uint16_t partialsSinceFull_ = 0;
};

}  // namespace mirror
