#include "templates/chat/ChatRenderer.h"

#include <algorithm>

#include "core/TemplateRegistry.h"
#include "core/Utf8.h"

namespace mirror::chat {

using namespace gen::chat;

bool ChatRenderer::onFull(const uint8_t* payload, size_t len) {
  Reader r(payload, len);
  ChatState s;
  if (!ChatState::decode(r, s) || !r.atEnd()) return false;
  if (s.draft_cursor > s.draft.size() || !utf8::isBoundary(s.draft, s.draft_cursor)) return false;
  state_ = std::move(s);
  return true;
}

bool ChatRenderer::applyOp(ChatState& s, const Op& op) {
  if (const auto* a = std::get_if<OpAppendMessage>(&op)) {
    s.messages.push_back(a->message);
    if (s.messages.size() > kMaxMessages) s.messages.erase(s.messages.begin(), s.messages.end() - kMaxMessages);
    return true;
  }
  if (const auto* e = std::get_if<OpEditLast>(&op)) {
    if (s.messages.empty()) return false;
    s.messages.back().text = e->text;
    return true;
  }
  if (std::get_if<OpDeleteLast>(&op)) {
    if (s.messages.empty()) return false;
    s.messages.pop_back();
    return true;
  }
  if (const auto* d = std::get_if<OpDraftEdit>(&op)) {
    const size_t pos = d->pos, del = d->del;
    if (pos > s.draft.size() || del > s.draft.size() - pos) return false;
    if (!utf8::isBoundary(s.draft, pos) || !utf8::isBoundary(s.draft, pos + del)) return false;
    if (s.draft.size() - del + d->ins.size() > kMaxDraftBytes) return false;
    s.draft.replace(pos, del, d->ins);
    s.draft_cursor = pos + d->ins.size();  // typing moves the cursor to the end of what was inserted
    return true;
  }
  if (const auto* d = std::get_if<OpDraftReplace>(&op)) {
    s.draft = d->text;
    s.draft_cursor = s.draft.size();
    return true;
  }
  if (const auto* c = std::get_if<OpSetDraftCursor>(&op)) {
    if (c->pos > s.draft.size() || !utf8::isBoundary(s.draft, c->pos)) return false;
    s.draft_cursor = c->pos;
    return true;
  }
  if (const auto* sc = std::get_if<OpScroll>(&op)) {
    s.scroll = sc->offset;
    return true;
  }
  if (const auto* t = std::get_if<OpSetTitle>(&op)) {
    s.title = t->title;
    return true;
  }
  if (const auto* u = std::get_if<OpSetUnread>(&op)) {
    s.unread = u->count;
    return true;
  }
  return false;
}

bool ChatRenderer::onDiff(const uint8_t* payload, size_t len) {
  Reader r(payload, len);
  const uint32_t n = r.readUvarint();
  if (!r.ok() || n > kMaxOpsPerDiff) return false;
  if (n == 0) return r.atEnd();
  if (n == 1) {  // a typed letter: applied in place, but only after the whole payload is known to be well formed
    Op op;
    if (!decodeOp(r, op) || !r.atEnd()) return false;
    return applyOp(state_, op);  // leaves the state untouched on failure
  }
  // Several ops apply to a copy so a half-applied diff is never committed.
  ChatState copy = state_;
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
  switch (it.kind) {
    case Item::Kind::Text:
      g.drawText(it.font, it.r.x, it.r.y, it.text, it.black);
      break;
    case Item::Kind::Rule:
    case Item::Kind::Bar:
      g.fillRect(it.r.x, it.r.y, it.r.w, it.r.h, true);
      break;
    case Item::Kind::ArrowDown:
    case Item::Kind::ArrowUp:
      for (int row = 0; row < it.r.h; ++row) {
        const int inset = it.kind == Item::Kind::ArrowDown ? row : it.r.h - 1 - row;  // the point is narrowest
        g.fillRect(it.r.x + inset, it.r.y + row, 1, 1, true);
        g.fillRect(it.r.x + it.r.w - 1 - inset, it.r.y + row, 1, 1, true);
      }
      break;
  }
}
}  // namespace

DirtyRects ChatRenderer::render(Gfx& gfx) {
  std::vector<Item> items = layout(gfx, state_);
  // Bars first: the unread badge's white text must land on its black box.
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
    // An item present in only one of the two frames changed pixels: a shifted line is dirty at its old and new
    // position, an untouched line is clean.
    std::vector<bool> used(prev_.size(), false);
    for (size_t n = 0; n < items.size(); ++n) {
      bool matched = false;
      for (size_t i = 0; i < prev_.size() && !matched; ++i) {
        if (!used[i] && prevKeys_[i] == keys[n]) used[i] = matched = true;
      }
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

bool ChatRenderer::onButton(Button b, std::vector<uint8_t>& eventPayload) {
  // Up/Down move the view at once through the messages held here; the phone's answer (the same absolute offset, or a
  // new window when the view leaves what this device holds) follows as a diff.
  const uint32_t oldest = state_.messages.empty() ? 0 : static_cast<uint32_t>(state_.messages.size() - 1);
  Event ev;
  if (b == Button::Up) {
    ev = EventScrollUp{1};
    if (state_.scroll < oldest) ++state_.scroll;
  } else if (b == Button::Down) {
    ev = EventScrollDown{1};
    if (state_.scroll > 0) --state_.scroll;
  } else if (b == Button::Top) {
    ev = EventScrollTop{};
  } else if (b == Button::Bottom) {
    ev = EventScrollBottom{};
  } else {
    return false;
  }
  Writer w;
  encodeEvent(ev, w);
  eventPayload = w.take();
  return w.ok();
}

std::string ChatRenderer::dump() const {
  const ChatState& s = state_;
  auto hex = [](const std::string& x) {
    static const char* d = "0123456789abcdef";
    std::string o;
    for (unsigned char c : x) {
      o += d[c >> 4];
      o += d[c & 15];
    }
    return o;
  };
  std::string out = "T:" + hex(s.title) + " U:" + std::to_string(s.unread) + " S:" + std::to_string(s.scroll) +
                    " D:" + hex(s.draft) + " C:" + std::to_string(s.draft_cursor) + " M:";
  for (size_t i = 0; i < s.messages.size(); ++i) {
    const auto& m = s.messages[i];
    if (i) out += ";";
    out += std::to_string(m.id) + "," + hex(m.sender) + "," + hex(m.text) + "," + std::to_string(m.timestamp) + "," +
           (m.outgoing ? "1" : "0");
  }
  return out;
}

MIRROR_REGISTER_TEMPLATE(ChatRenderer, gen::chat::kTemplateId, 1, gen::chat::kSchemaVersion)

}  // namespace mirror::chat
