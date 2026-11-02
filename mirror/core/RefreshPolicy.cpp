#include "core/RefreshPolicy.h"

#include <algorithm>

namespace mirror {

namespace {
bool near(const Rect& a, const Rect& b, int gap) {
  return a.x - gap < b.x + b.w && b.x - gap < a.x + a.w && a.y - gap < b.y + b.h && b.y - gap < a.y + a.h;
}
}  // namespace

std::vector<Rect> RefreshPolicy::mergeRects(std::vector<Rect> rects, int gap) {
  rects.erase(std::remove_if(rects.begin(), rects.end(), [](const Rect& r) { return r.empty(); }), rects.end());
  bool merged = true;
  while (merged) {
    merged = false;
    for (size_t i = 0; i < rects.size() && !merged; ++i) {
      for (size_t j = i + 1; j < rects.size() && !merged; ++j) {
        if (!near(rects[i], rects[j], gap)) continue;
        const int x0 = std::min(rects[i].x, rects[j].x), y0 = std::min(rects[i].y, rects[j].y);
        const int x1 = std::max(rects[i].x + rects[i].w, rects[j].x + rects[j].w);
        const int y1 = std::max(rects[i].y + rects[i].h, rects[j].y + rects[j].h);
        rects[i] = Rect{x0, y0, x1 - x0, y1 - y0};
        rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(j));
        merged = true;
      }
    }
  }
  std::sort(rects.begin(), rects.end(),
            [](const Rect& a, const Rect& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
  return rects;
}

Rect RefreshPolicy::alignToBytes(Rect r, int screenW) {
  const int x0 = std::max(0, r.x) & ~7;
  int x1 = std::min(screenW, r.x + r.w);
  x1 = (x1 + 7) & ~7;
  x1 = std::min(x1, (screenW + 7) & ~7);
  return Rect{x0, r.y, x1 - x0, r.h};
}

RefreshPlan RefreshPolicy::plan(const DirtyRects& dirty, int screenW, int screenH, const RefreshOverrides& ov,
                                bool forceFull) {
  RefreshPlan p;
  if (forceFull || dirty.full) {
    p.kind = RefreshKind::Full;
    p.rects = {Rect{0, 0, screenW, screenH}};
    partialsSinceFull_ = 0;
    return p;
  }
  if (dirty.rects.empty()) return p;
  const uint16_t every = ov.fullRefreshEveryN ? ov.fullRefreshEveryN : cfg_.fullRefreshEveryN;
  if (++partialsSinceFull_ >= every) {
    p.kind = RefreshKind::Full;
    p.rects = {Rect{0, 0, screenW, screenH}};
    partialsSinceFull_ = 0;
    return p;
  }
  p.kind = RefreshKind::Partial;
  for (const Rect& r : mergeRects(dirty.rects, cfg_.mergeGapPx)) {
    Rect a = alignToBytes(r, screenW);
    a.y = std::max(0, a.y);
    a.h = std::min(a.h, screenH - a.y);
    p.rects.push_back(a);
  }
  return p;
}

}  // namespace mirror
