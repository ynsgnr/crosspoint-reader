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
    if (s.draft.size() - del + d->ins.size() > 1024) return false;
    s.draft.replace(pos, del, d->ins);
    return true;
  }
  if (const auto* d = std::get_if<OpDraftReplace>(&op)) {
    s.draft = d->text;
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
  ChatState work = state_;  // all-or-nothing: the committed state never holds a half-applied diff
  for (uint32_t i = 0; i < n; ++i) {
    Op op;
    if (!decodeOp(r, op) || !applyOp(work, op)) return false;
  }
  if (!r.atEnd()) return false;
  state_ = std::move(work);
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
  }
}
}  // namespace

DirtyRects ChatRenderer::render(Gfx& gfx) {
  std::vector<Item> items = layout(gfx, state_);
  // Draw the badge background before its text: bars first keeps white-on-black text visible.
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    return (a.kind == Item::Kind::Text) < (b.kind == Item::Kind::Text);
  });
  gfx.clear();
  for (const Item& it : items) paint(gfx, it);

  DirtyRects d;
  if (!havePrev_ || prevW_ != gfx.width() || prevH_ != gfx.height()) {
    d.full = true;
  } else {
    // Items present in only one of the two frames changed pixels. Matching by key makes a shifted line dirty
    // at both its old and new position, and an untouched line clean.
    std::vector<bool> used(prev_.size(), false);
    for (const Item& n : items) {
      const uint64_t k = n.key();
      bool matched = false;
      for (size_t i = 0; i < prev_.size(); ++i) {
        if (!used[i] && prev_[i].key() == k) {
          used[i] = matched = true;
          break;
        }
      }
      if (!matched) d.rects.push_back(n.r);
    }
    for (size_t i = 0; i < prev_.size(); ++i)
      if (!used[i]) d.rects.push_back(prev_[i].r);
  }
  prev_ = std::move(items);
  havePrev_ = true;
  prevW_ = gfx.width();
  prevH_ = gfx.height();
  return d;
}

bool ChatRenderer::onButton(Button b, std::vector<uint8_t>& eventPayload) {
  Event ev;
  if (b == Button::Up)
    ev = EventScrollUp{1};
  else if (b == Button::Down)
    ev = EventScrollDown{1};
  else
    return false;
  Writer w;
  encodeEvent(ev, w);
  eventPayload = w.take();
  return w.ok();
}

std::string dumpState(const ChatState& s) {
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
                    " D:" + hex(s.draft) + " M:";
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
