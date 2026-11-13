#pragma once
// What the scroll buttons mean for any template whose view moves through held items (Chat's messages, the List's rows).
// The events are the same for every such template; each template's generated header names its own event types.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/TemplateRenderer.h"

namespace mirror {

// Up/Down are one step, Top/Bottom a jump to the first/last item the phone holds; other buttons are not scrolling.
template <class Event, class ScrollUp, class ScrollDown, class ScrollTop, class ScrollBottom>
std::optional<Event> scrollEventFor(Button b) {
  switch (b) {
    case Button::Up:
      return Event{ScrollUp{1}};
    case Button::Down:
      return Event{ScrollDown{1}};
    case Button::Top:
      return Event{ScrollTop{}};
    case Button::Bottom:
      return Event{ScrollBottom{}};
    default:
      return std::nullopt;
  }
}

// The scroll offset after one Up/Down press: how many items lie beyond the view's end, 0 = the last item is in view,
// held - 1 = the first. Mirrors scrollOffsetAfter in the Android app, so the device moves at once exactly as the
// phone's answer will say. Top/Bottom change nothing here: they wait for the phone.
inline uint32_t scrollOffsetAfterPress(Button b, uint32_t offset, size_t held) {
  const uint32_t last = held ? static_cast<uint32_t>(held - 1) : 0;
  if (b == Button::Up) return std::min(offset + 1, last);
  if (b == Button::Down) return std::min(offset > 0 ? offset - 1 : 0, last);
  return std::min(offset, last);
}

}  // namespace mirror
