#include "templates/list/ListRenderer.h"

#include <algorithm>

#include "core/ScrollButtons.h"
#include "core/TemplateRegistry.h"

namespace mirror::list {

using namespace gen::list;

bool ListRenderer::onFull(const uint8_t* payload, size_t len) {
  Reader r(payload, len);
  ListState s;
  if (!ListState::decode(r, s) || !r.atEnd() || s.items.size() > kMaxItems) return false;
  state_ = std::move(s);
  return true;
}

bool ListRenderer::applyOp(ListState& s, const Op& op) {
  if (const auto* t = std::get_if<OpSetTitle>(&op)) {
    s.title = t->title;
    return true;
  }
  if (const auto* set = std::get_if<OpSetItem>(&op)) {
    if (set->index >= s.items.size()) return false;
    s.items[set->index] = set->item;
    return true;
  }
  if (const auto* a = std::get_if<OpAppendItem>(&op)) {
    if (s.items.size() >= kMaxItems) return false;
    s.items.push_back(a->item);
    return true;
  }
  if (const auto* t = std::get_if<OpTruncateItems>(&op)) {
    if (t->count > s.items.size()) return false;
    s.items.resize(t->count);
    return true;
  }
  if (const auto* sel = std::get_if<OpSetSelected>(&op)) {
    s.selected = sel->id;
    return true;
  }
  return false;
}

bool ListRenderer::onDiff(const uint8_t* payload, size_t len) {
  Reader r(payload, len);
  const uint32_t n = r.readUvarint();
  if (!r.ok() || n > kMaxOpsPerDiff) return false;
  // Always applied to a copy so a half-applied diff is never committed.
  ListState copy = state_;
  for (uint32_t i = 0; i < n; ++i) {
    Op op;
    if (!decodeOp(r, op) || !applyOp(copy, op)) return false;
  }
  if (!r.atEnd()) return false;
  state_ = std::move(copy);
  return true;
}

namespace {
void paint(Gfx& g, const Item& it) {
  if (it.kind == Item::Kind::Text)
    g.drawText(it.font, it.r.x, it.r.y, it.text, it.black);
  else
    g.fillRect(it.r.x, it.r.y, it.r.w, it.r.h, it.black);
}
}  // namespace

DirtyRects ListRenderer::render(Gfx& gfx) {
  first_ = keepVisible(gfx, state_, first_);
  Layout l = layout(gfx, state_, first_);
  rows_ = std::move(l.rows);
  std::vector<Item> items = std::move(l.items);
  // Fills first: the selected row's white text must land on its black box.
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    return (a.kind == Item::Kind::Text) < (b.kind == Item::Kind::Text);
  });
  gfx.clear();
  std::vector<uint64_t> keys;
  keys.reserve(items.size());
  for (const Item& it : items) {
    paint(gfx, it);
    keys.push_back(it.key());
  }

  DirtyRects d;
  if (!havePrev_ || prevW_ != gfx.width() || prevH_ != gfx.height()) {
    d.full = true;
  } else {
    // An item present in only one of the two frames changed pixels (as in ChatRenderer::render).
    std::vector<bool> used(prev_.size(), false);
    for (size_t n = 0; n < items.size(); ++n) {
      bool matched = false;
      for (size_t i = 0; i < prev_.size() && !matched; ++i)
        if (!used[i] && prevKeys_[i] == keys[n]) used[i] = matched = true;
      if (!matched) d.rects.push_back(items[n].r);
    }
    for (size_t i = 0; i < prev_.size(); ++i)
      if (!used[i]) d.rects.push_back(prev_[i].r);
  }
  prev_ = std::move(items);
  prevKeys_ = std::move(keys);
  havePrev_ = true;
  prevW_ = gfx.width();
  prevH_ = gfx.height();
  return d;
}

size_t ListRenderer::selectedIndex() const {
  for (size_t i = 0; i < state_.items.size(); ++i)
    if (state_.items[i].id == state_.selected) return i;
  return state_.items.size();
}

bool ListRenderer::onButton(Button b, std::vector<uint8_t>& eventPayload) {
  Writer w;
  if (b == Button::Confirm) {
    if (selectedIndex() >= state_.items.size()) return false;
    encodeEvent(EventActivate{state_.selected}, w);
  } else {
    // The same scroll events and offset arithmetic as the chat; the offset counts the rows after the selected one.
    const auto ev = scrollEventFor<Event, EventScrollUp, EventScrollDown, EventScrollTop, EventScrollBottom>(b);
    const size_t n = state_.items.size();
    if (!ev || n == 0) return false;
    const size_t sel = selectedIndex();
    const uint32_t offset =
        scrollOffsetAfterPress(b, sel < n ? static_cast<uint32_t>(n - 1 - sel) : static_cast<uint32_t>(n), n);
    if (b == Button::Up || b == Button::Down) state_.selected = state_.items[n - 1 - offset].id;
    encodeEvent(*ev, w);
  }
  eventPayload = w.take();
  return w.ok();
}

bool ListRenderer::onTap(int x, int y, std::vector<uint8_t>& eventPayload) {
  for (const RowRect& row : rows_) {
    if (x < row.r.x || x >= row.r.x + row.r.w || y < row.r.y || y >= row.r.y + row.r.h) continue;
    state_.selected = row.id;  // the tapped row shows as selected while the phone opens it
    Writer w;
    encodeEvent(EventActivate{row.id}, w);
    eventPayload = w.take();
    return w.ok();
  }
  return false;
}

std::string ListRenderer::dump() const {
  const ListState& s = state_;
  auto hex = [](const std::string& x) {
    static const char* d = "0123456789abcdef";
    std::string o;
    for (unsigned char c : x) {
      o += d[c >> 4];
      o += d[c & 15];
    }
    return o;
  };
  std::string out = "T:" + hex(s.title) + " S:" + std::to_string(s.selected) + " I:";
  for (size_t i = 0; i < s.items.size(); ++i) {
    const auto& it = s.items[i];
    if (i) out += ";";
    out += std::to_string(it.id) + "," + hex(it.label) + "," + hex(it.detail) + "," + hex(it.meta) + "," +
           std::to_string(it.badge);
  }
  return out;
}

MIRROR_REGISTER_TEMPLATE(ListRenderer, gen::list::kTemplateId, 1, gen::list::kSchemaVersion)

}  // namespace mirror::list
