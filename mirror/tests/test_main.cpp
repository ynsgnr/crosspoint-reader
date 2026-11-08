// Host tests for the portable mirror layer. No framework: CHECK prints and counts failures.
#include <dirent.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/BandedGfx.h"
#include "core/Chord.h"
#include "core/Crc16.h"
#include "core/Envelope.h"
#include "core/Frame.h"
#include "core/RefreshPolicy.h"
#include "core/Session.h"
#include "core/Utf8.h"
#include "sim/SimGfx.h"
#include "sim/Transcript.h"
#include "templates/chat/ChatRenderer.h"

using namespace mirror;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                               \
  do {                                                         \
    ++g_checks;                                                \
    if (!(c)) {                                                \
      ++g_fail;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                          \
  } while (0)

static void testCrcAndWire() {
  const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK(crc16(v, 9) == 0x29B1);
  Writer w;
  w.writeUvarint(0);
  w.writeUvarint(300);
  w.writeUvarint(0xFFFFFFFFu);
  w.writeSvarint(-1);
  w.writeSvarint(INT32_MIN);
  w.writeString("h\xC3\xA9", 8);
  Reader r(w.bytes().data(), w.bytes().size());
  CHECK(r.readUvarint() == 0 && r.readUvarint() == 300 && r.readUvarint() == 0xFFFFFFFFu);
  CHECK(r.readSvarint() == -1 && r.readSvarint() == INT32_MIN);
  CHECK(r.readString(8) == "h\xC3\xA9" && r.atEnd() && r.ok());
  const uint8_t overflow[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
  Reader o(overflow, 5);
  o.readUvarint();
  CHECK(!o.ok());
  Reader t(w.bytes().data(), 2);
  t.readUvarint();
  t.readUvarint();
  t.readUvarint();
  CHECK(!t.ok());
}

static void testEnvelopeStream() {
  Envelope a;
  a.templateId = 1;
  a.schemaVersion = 1;
  a.flags = gen::kFlagFull;
  a.seq = 1;
  a.payload = {1, 2, 3};
  Envelope b = a;
  b.flags = gen::kFlagDiff;
  b.seq = 2;
  b.baseSeq = 1;
  b.payload = {9};
  std::vector<uint8_t> bytes = encodeEnvelope(a), bb = encodeEnvelope(b);
  bytes.insert(bytes.begin(), {0x00, 0x11});  // garbage before the first frame
  bytes.insert(bytes.end(), bb.begin(), bb.end());
  EnvelopeStreamParser p;
  std::vector<Envelope> got;
  for (size_t i = 0; i < bytes.size(); i += 5) p.feed(bytes.data() + i, std::min<size_t>(5, bytes.size() - i), got);
  CHECK(got.size() == 2 && got[0].seq == 1 && got[1].baseSeq == 1 && got[0].payload.size() == 3);
  auto bad = encodeEnvelope(a);
  bad[12] ^= 1;
  bad.insert(bad.end(), bb.begin(), bb.end());
  EnvelopeStreamParser p2;
  got.clear();
  p2.feed(bad.data(), bad.size(), got);
  CHECK(got.size() == 1 && got[0].seq == 2 && p2.crcErrors() >= 1);
}

static void testRefreshPolicy() {
  RefreshPolicy pol;
  DirtyRects d;
  d.rects = {Rect{13, 700, 100, 20}, Rect{15, 718, 50, 20}};
  RefreshPlan p = pol.plan(d, 480, 800, {}, false);
  CHECK(p.kind == RefreshKind::Partial && p.rects.size() == 1);
  CHECK(p.rects[0].x % 8 == 0 && p.rects[0].w % 8 == 0 && p.rects[0].x <= 13 && p.rects[0].x + p.rects[0].w >= 113);
  CHECK(pol.plan(DirtyRects{}, 480, 800, {}, false).kind == RefreshKind::None);
  for (int i = 0; i < 18; ++i) CHECK(pol.plan(d, 480, 800, {}, false).kind == RefreshKind::Partial);
  CHECK(pol.plan(d, 480, 800, {}, false).kind == RefreshKind::Full);  // 20th
  CHECK(pol.partialsSinceFull() == 0);
  CHECK(pol.plan(d, 480, 800, {}, true).kind == RefreshKind::Full);
  RefreshOverrides ov;
  ov.fullRefreshEveryN = 2;
  CHECK(pol.plan(d, 480, 800, ov, false).kind == RefreshKind::Partial);
  CHECK(pol.plan(d, 480, 800, ov, false).kind == RefreshKind::Full);
}

static std::vector<uint8_t> chatFull(const gen::chat::ChatState& s) {
  Writer w;
  s.encode(w);
  return w.take();
}

static void testChatRenderingAndDirtyRects() {
  sim::SimGfx g(480, 800);
  chat::ChatRenderer r;
  gen::chat::ChatState s;
  s.title = "Alice";
  s.messages = {{1, "Alice", "Hello there, this is a long message that must wrap over several lines on the panel.",
                 3600 * 12 + 60 * 5, false},
                {2, "Me", "Short reply", 3600 * 12 + 60 * 6, true}};
  auto bytes = chatFull(s);
  CHECK(r.onFull(bytes.data(), bytes.size()));
  DirtyRects d = r.render(g);
  CHECK(d.full);
  int dark = 0;
  for (int y = 0; y < 800; ++y)
    for (int x = 0; x < 480; ++x) dark += g.black(x, y);
  CHECK(dark > 2000);

  // Typing one letter: only the draft area may be dirty.
  Writer w;
  w.writeUvarint(1);
  encodeOp(gen::chat::OpDraftEdit{0, 0, "a"}, w);
  CHECK(r.onDiff(w.bytes().data(), w.bytes().size()));
  d = r.render(g);
  CHECK(!d.full && !d.rects.empty());
  const int bodyH = g.lineHeight(Font::Body);
  for (const Rect& rc : d.rects) CHECK(rc.y >= 800 - 3 * bodyH - 16 && rc.h <= 2 * bodyH);

  // A diff that re-renders identically produces no dirty rects.
  d = r.render(g);
  CHECK(d.empty());

  // Invalid diff leaves state untouched.
  Writer bad;
  bad.writeUvarint(2);
  encodeOp(gen::chat::OpDraftEdit{0, 0, "z"}, bad);
  encodeOp(gen::chat::OpDraftEdit{99, 0, "q"}, bad);
  CHECK(!r.onDiff(bad.bytes().data(), bad.bytes().size()));
  CHECK(r.state().draft == "a");

  // Long text never leaves the screen: every item is inside the panel.
  gen::chat::ChatState big = s;
  for (int i = 0; i < 24; ++i)
    big.messages.push_back({uint32_t(10 + i), "Bob", std::string(300, 'w') + " end", 100, i % 2 == 0});
  big.draft = std::string(400, 'd');
  for (const auto& it : chat::layout(g, big)) CHECK(it.r.y >= 0 && it.r.y + it.r.h <= 800 && it.r.x >= 0);
}

static void testWrap() {
  sim::SimGfx g(480, 800);
  auto lines = chat::wrapText(g, Font::Body, "aaa bbb\nccc " + std::string(200, 'x') + " \xC3\xA9", 200);
  CHECK(lines.size() >= 4);
  for (const auto& l : lines) CHECK(g.textWidth(Font::Body, l) <= 200);
  CHECK(chat::formatTime(3600 * 7 + 60 * 9) == "07:09");
}

static void testRegistryAndCaps() {
  const auto& reg = TemplateRegistry::instance();
  CHECK(reg.find(1) != nullptr && reg.find(255) == nullptr);
  sim::SimGfx g(480, 800);
  Session s(reg, [](const std::vector<uint8_t>&) {});
  auto caps = s.capabilities(g, "CrossPoint");
  Reader r(caps.data(), caps.size());
  gen::Capabilities c;
  CHECK(gen::Capabilities::decode(r, c) && r.atEnd());
  CHECK(c.max_payload == 2048 && c.screen_width == 480 && c.templates.size() == reg.entries().size() &&
        c.templates[0].id == 1 && c.fonts.size() == 3);
}

namespace {
struct Device {
  std::vector<Envelope> out;
  Session session{TemplateRegistry::instance(), [this](const std::vector<uint8_t>& f) {
                    EnvelopeStreamParser p;
                    p.feed(f.data(), f.size(), out);
                  }};
  void send(uint8_t flags, uint16_t seq, uint16_t base, std::vector<uint8_t> payload, uint8_t templateId = 1,
            uint8_t schema = 1, uint8_t proto = gen::kProtocolVersion) {
    Envelope e;
    e.protocolVersion = proto;
    e.templateId = templateId;
    e.schemaVersion = schema;
    e.flags = flags;
    e.seq = seq;
    e.baseSeq = base;
    e.payload = std::move(payload);
    const auto bytes = encodeEnvelope(e);
    session.feed(bytes.data(), bytes.size());
  }
  void full(uint16_t seq, const gen::chat::ChatState& s, uint8_t extraFlags = 0) {
    send(gen::kFlagFull | extraFlags, seq, 0, chatFull(s));
  }
  void draftDiff(uint16_t seq, uint16_t base, uint32_t pos, uint32_t del, const std::string& ins) {
    Writer w;
    w.writeUvarint(1);
    encodeOp(gen::chat::OpDraftEdit{pos, del, ins}, w);
    send(gen::kFlagDiff, seq, base, w.take());
  }
  uint8_t errorCode() const {
    Reader r(out.back().payload.data(), out.back().payload.size());
    gen::ControlEvent c;
    gen::ControlEvent::decode(r, c);
    return c.error_code;
  }
  const chat::ChatRenderer& chatState() { return *static_cast<chat::ChatRenderer*>(session.active()); }
};
}  // namespace

static void testSessionAckSwitchAndDiffs() {
  Device d;
  CHECK(d.session.active() == nullptr && !d.session.onButton(Button::Up));
  gen::chat::ChatState s;
  s.title = "T";
  d.full(5, s, gen::kFlagAckRequest);
  CHECK(d.session.active() && d.session.lastApplied() == 5);
  CHECK(d.out.size() == 1 && d.out[0].has(gen::kFlagAck) && d.out[0].baseSeq == 5);
  CHECK(d.session.takeTemplateSwitched() && !d.session.takeTemplateSwitched());

  d.draftDiff(6, 5, 0, 0, "hi");
  CHECK(d.chatState().state().draft == "hi" && d.session.lastApplied() == 6 && !d.session.takeTemplateSwitched());
  CHECK(d.out.size() == 1);  // no ack requested
}

static void testSessionResyncCases() {
  Device d;
  d.draftDiff(1, 0, 0, 0, "x");  // diff before any state
  CHECK(d.out.size() == 1 && d.out[0].has(gen::kFlagResync) && d.out[0].baseSeq == 0);
  CHECK(d.session.takeResyncHappened());

  d.full(10, {});
  d.out.clear();
  d.draftDiff(12, 11, 0, 0, "x");  // gap: base 11 is not the applied seq 10
  CHECK(d.out.size() == 1 && d.out[0].has(gen::kFlagResync) && d.out[0].baseSeq == 10);
  CHECK(d.chatState().state().draft.empty());

  d.out.clear();
  d.draftDiff(13, 12, 0, 0, "y");  // more diffs in flight: dropped, no resync storm
  d.draftDiff(14, 13, 0, 0, "z");
  CHECK(d.out.empty() && d.chatState().state().draft.empty());

  d.full(15, {});                   // the phone's full reply ends the wait
  d.draftDiff(16, 15, 99, 0, "x");  // invalid for the state: position past the end
  CHECK(d.out.size() == 1 && d.out[0].has(gen::kFlagResync) && d.session.lastApplied() == 15);

  d.out.clear();
  d.session.requestResync();
  CHECK(d.out[0].templateId == 0 && d.out[0].has(gen::kFlagResync));

  d.full(20, {});
  d.out.clear();
  auto bad = encodeEnvelope(Envelope{});
  bad[12] ^= 1;
  d.session.feed(bad.data(), bad.size());  // CRC failure
  CHECK(d.out.size() == 1 && d.out[0].templateId == 0 && d.out[0].has(gen::kFlagResync));
}

static void testStreamParserRejectsOversizedLengths() {
  Envelope e;
  e.templateId = 1;
  e.schemaVersion = 1;
  e.flags = gen::kFlagFull;
  e.seq = 1;
  e.payload = {1, 2, 3};
  const auto frame = encodeEnvelope(e);
  // A stray magic byte with an absurd length must not swallow the real frame behind it.
  std::vector<uint8_t> bytes = {static_cast<uint8_t>(gen::kMagic), 1, 1, 1, 1, 0, 0, 0, 0, 0xFF, 0xFF};
  bytes.insert(bytes.end(), frame.begin(), frame.end());
  EnvelopeStreamParser p;
  std::vector<Envelope> got;
  p.feed(bytes.data(), bytes.size(), got);
  CHECK(got.size() == 1 && got[0].seq == 1);

  EnvelopeStreamParser small(2);  // payload above the configured maximum is rejected
  got.clear();
  small.feed(frame.data(), frame.size(), got);
  CHECK(got.empty());
}

static void testWriterOverflowAndRegistry() {
  Writer w;
  w.writeString(std::string(10, 'a'), 5);
  CHECK(!w.ok());
  auto& reg = TemplateRegistry::instance();
  const size_t before = reg.entries().size();
  reg.add(TemplateEntry{1, 1, 1, nullptr});  // duplicate id is ignored
  CHECK(reg.entries().size() == before && reg.find(1)->create != nullptr);
}

static void testChatLayoutStates() {
  sim::SimGfx g(480, 800);
  gen::chat::ChatState s;
  auto texts = [&] {
    std::vector<std::string> out;
    for (const auto& it : chat::layout(g, s))
      if (it.kind == chat::Item::Kind::Text) out.push_back(it.text);
    return out;
  };
  auto has = [&](const std::string& t) {
    for (const auto& x : texts())
      if (x == t) return true;
    return false;
  };
  CHECK(has("No messages yet"));

  s.messages = {{1, "Bob", "a", 0, false},
                {2, "Me", "one two three four five six seven eight nine ten eleven twelve", 0, true}};
  s.scroll = 1;
  CHECK(has("v1") && !has("No messages yet"));

  // An outgoing multi-line message is one block: every text line starts at the same x.
  s.scroll = 0;
  s.messages.erase(s.messages.begin());
  int x = -1;
  bool aligned = true;
  for (const auto& it : chat::layout(g, s)) {
    if (it.kind != chat::Item::Kind::Text || it.font == Font::Title || it.text == ">") continue;
    if (it.r.y > 690) continue;  // above the draft area
    if (x < 0) x = it.r.x;
    aligned &= it.r.x == x;
  }
  CHECK(aligned);

  // A message taller than the body shows an ellipsis line, then its last lines.
  s.messages = {{1, "Bob", std::string(2000, 'w') + " end", 0, false}};
  CHECK(has("\xE2\x80\xA6"));
}

static void testSessionErrors() {
  Device d;
  d.send(gen::kFlagFull, 1, 0, {}, 9);
  CHECK(d.errorCode() == gen::kErrUnknownTemplate);
  d.send(gen::kFlagFull, 1, 0, chatFull({}), 1, 9);
  CHECK(d.errorCode() == gen::kErrSchemaMismatch);
  d.send(gen::kFlagFull, 1, 0, chatFull({}), 1, 1, 7);
  CHECK(d.errorCode() == gen::kErrBadProtocol);
  d.send(gen::kFlagFull, 1, 0, {0xFF, 0xFF});
  CHECK(d.errorCode() == gen::kErrBadPayload && d.session.active() == nullptr);
  CHECK(d.out.size() == 4 && d.out[0].has(gen::kFlagEvent) && d.out[0].templateId == 0);
}

static void testSessionButtons() {
  Device d;
  d.full(1, {});
  d.out.clear();
  CHECK(d.session.onButton(Button::Up) && d.out.size() == 1 && d.out[0].has(gen::kFlagEvent) &&
        d.out[0].templateId == 1);
  CHECK(!d.session.onButton(Button::Left));
  CHECK(d.out.size() == 1);
  // Top and Bottom (the simulator's jumps to the ends) are one event frame each.
  CHECK(d.session.onButton(Button::Top) && d.session.onButton(Button::Bottom) && d.out.size() == 3);
  CHECK(d.out[1].has(gen::kFlagEvent) && d.out[2].has(gen::kFlagEvent));
}

// The draft box grows with the text and follows the cursor.
static void testDraftGrowsAndFollowsCursor() {
  sim::SimGfx g(480, 800);
  const int width = 480 - 2 * 8 - g.textWidth(Font::Body, "> ") - 4;
  // Ranges cover the text: a newline or a space separates lines, nothing else is skipped.
  const std::string text = "ab cd\nef";
  const auto small = chat::wrapRanges(g, Font::Body, text, width);
  CHECK(small.size() == 2 && small[0].begin == 0 && small[0].end == 5 && small[1].begin == 6 && small[1].end == 8);
  CHECK(chat::wrapRanges(g, Font::Body, "", width).size() == 1);
  // Hard breaks never split a multi-byte character.
  std::string wide;
  for (int i = 0; i < 80; ++i) wide += "\xC3\xA9";  // é
  for (const auto& l : chat::wrapRanges(g, Font::Body, wide, width)) {
    CHECK(utf8::isBoundary(wide, l.begin) && utf8::isBoundary(wide, l.end));
    CHECK(g.textWidth(Font::Body, wide.substr(l.begin, l.end - l.begin)) <= width);
  }
  std::string many;
  for (int i = 0; i < 40; ++i) many += "word" + std::to_string(i) + " ";
  const auto lines = chat::wrapRanges(g, Font::Body, many, width);
  CHECK(lines.size() > 4);
  for (size_t i = 0; i < lines.size(); ++i) {
    CHECK(g.textWidth(Font::Body, many.substr(lines[i].begin, lines[i].end - lines[i].begin)) <= width);
    if (i) CHECK(lines[i].begin >= lines[i - 1].end);
  }

  gen::chat::ChatState s;
  s.messages = {{1, "Bob", "hi", 0, false}};
  auto ruleY = [&](const gen::chat::ChatState& st) {  // the last rule is the draft box's
    int y = -1;
    for (const auto& it : chat::layout(g, st))
      if (it.kind == chat::Item::Kind::Rule) y = it.r.y;
    return y;
  };
  auto draftTexts = [&](const gen::chat::ChatState& st) {
    std::vector<std::string> out;
    const int top = ruleY(st);
    for (const auto& it : chat::layout(g, st))
      if (it.kind == chat::Item::Kind::Text && it.r.y > top && it.r.x > 8) out.push_back(it.text);
    return out;
  };
  const int shortTop = ruleY(s);
  s.draft = many;
  s.draft_cursor = many.size();
  CHECK(ruleY(s) < shortTop);                                       // taller box
  CHECK(draftTexts(s).back().find("word39") != std::string::npos);  // cursor at the end: the end is shown
  s.draft_cursor = 0;
  CHECK(draftTexts(s).front().find("word0") == 0);  // cursor at the start: the box shows the start
  s.draft_cursor = many.find("word20");             // cursor in the middle: its line is visible
  bool middle = false;
  for (const auto& t : draftTexts(s)) middle |= t.find("word20") != std::string::npos;
  CHECK(middle);
  // Absurd drafts stay on screen and leave room for the header and a message.
  s.draft = std::string(1000, 'x') + " " + many + many + many + many;
  s.draft_cursor = s.draft.size();
  for (const auto& it : chat::layout(g, s)) CHECK(it.r.y >= 0 && it.r.y + it.r.h <= 800 && it.r.x >= 0);
  bool messageShown = false;  // the box leaves room for at least one message
  for (const auto& it : chat::layout(g, s)) messageShown |= it.kind == chat::Item::Kind::Text && it.text == "hi";
  CHECK(messageShown);
}

// A single-op diff with trailing bytes is rejected before anything is applied.
static void testSingleOpDiffWithTrailingBytesLeavesTheStateAlone() {
  sim::SimGfx g(480, 800);
  chat::ChatRenderer r;
  gen::chat::ChatState s;
  s.draft = "a";
  s.draft_cursor = 1;
  const auto full = chatFull(s);
  CHECK(r.onFull(full.data(), full.size()));
  Writer w;
  w.writeUvarint(1);
  encodeOp(gen::chat::OpDraftEdit{1, 0, "b"}, w);
  std::vector<uint8_t> bytes = w.bytes();
  bytes.push_back(0x55);  // trailing garbage
  CHECK(!r.onDiff(bytes.data(), bytes.size()));
  CHECK(r.state().draft == "a");
  CHECK(r.onDiff(w.bytes().data(), w.bytes().size()) && r.state().draft == "ab");
}

static void testChatOpSemantics() {
  using namespace gen::chat;
  ChatState s;
  CHECK(!chat::ChatRenderer::applyOp(s, OpEditLast{"x"}) && !chat::ChatRenderer::applyOp(s, OpDeleteLast{}));
  for (uint32_t i = 0; i < 30; ++i) CHECK(chat::ChatRenderer::applyOp(s, OpAppendMessage{{i, "a", "t", 0, false}}));
  CHECK(s.messages.size() == 24 && s.messages.front().id == 6);    // oldest dropped
  s.draft = "\xC3\xA9";                                            // one two-byte code point
  CHECK(!chat::ChatRenderer::applyOp(s, OpDraftEdit{1, 0, "x"}));  // inside the code point
  CHECK(!chat::ChatRenderer::applyOp(s, OpDraftEdit{0, 3, ""}));   // past the end
  CHECK(chat::ChatRenderer::applyOp(s, OpDraftEdit{2, 0, "!"}) && s.draft == "\xC3\xA9!");
  CHECK(!chat::ChatRenderer::applyOp(s, OpDraftEdit{0, 0, std::string(1100, 'x')}));  // over the draft limit
}

static void testWrapHardBreakAndAlignment() {
  sim::SimGfx g(480, 800);
  for (const auto& l : chat::wrapText(g, Font::Body, std::string(300, 'W'), 100))
    CHECK(g.textWidth(Font::Body, l) <= 100);
  CHECK(RefreshPolicy::alignToBytes(Rect{13, 5, 10, 4}, 480) == (Rect{8, 5, 16, 4}));
  CHECK(RefreshPolicy::alignToBytes(Rect{470, 5, 10, 4}, 480) == (Rect{464, 5, 16, 4}));
}

static void testControlChannel() {
  Device d;
  CHECK(!d.session.phoneStatus() && d.session.phoneDump() == "-");
  Writer w;
  gen::PhoneStatus{true, 25, true, {{"Signal"}, {"WhatsApp"}}}.encode(w);
  d.send(gen::kFlagStatus, 1, 0, w.take(), 0, 0);
  CHECK(d.out.empty() && d.session.phoneStatus() && d.session.phoneStatus()->minutes_left == 25);
  CHECK(d.session.phoneDump() == "dark=1 minutes=25 locked=1 apps=Signal,WhatsApp");
  d.send(gen::kFlagStatus, 2, 0, {0xFF}, 0, 0);  // garbage status is ignored
  CHECK(d.session.phoneDump() == "dark=1 minutes=25 locked=1 apps=Signal,WhatsApp" && d.out.empty());

  d.session.sendCommand(gen::kCmdToggleDarkMode);
  CHECK(d.out.size() == 1 && d.out[0].templateId == 0 && d.out[0].has(gen::kFlagCommand));
  Reader r(d.out[0].payload.data(), d.out[0].payload.size());
  gen::ControlCommand c;
  CHECK(gen::ControlCommand::decode(r, c) && c.command == gen::kCmdToggleDarkMode && c.arg == 0);

  d.out.clear();
  d.session.sendCommand(gen::kCmdHidStatus, 2);
  Reader r2(d.out[0].payload.data(), d.out[0].payload.size());
  CHECK(d.out.size() == 1 && gen::ControlCommand::decode(r2, c) && c.command == gen::kCmdHidStatus && c.arg == 2);

  d.out.clear();
  d.session.sendCommand(gen::kCmdLaunchApp, 1);  // the launcher picks the phone's second app
  Reader r3(d.out[0].payload.data(), d.out[0].payload.size());
  CHECK(d.out.size() == 1 && gen::ControlCommand::decode(r3, c) && c.command == gen::kCmdLaunchApp && c.arg == 1);
}

static void testDefaultChordAndDigits() {
  using namespace mirror;
  CHECK(kDefaultChord.mods == (kModCtrl | kModAlt | kModShift) && kDefaultChord.key == 0x10);  // Ctrl+Alt+Shift+M
  CHECK(digitUsage(1) == 0x1E && digitUsage(9) == 0x26 && digitUsage(0) == 0x27);
}

static void testBandedGfxLeavesTheTopBandFree() {
  sim::SimGfx g(100, 100);
  BandedGfx banded(g, 20);
  CHECK(banded.height() == 80 && banded.width() == 100);
  banded.fillRect(0, 0, 10, 10, true);
  CHECK(g.black(0, 20) && !g.black(0, 19) && !g.black(0, 9));
  // A whole template frame never touches the band, which carries the status text.
  chat::ChatRenderer r;
  gen::chat::ChatState s;
  s.title = "Title";
  auto bytes = chatFull(s);
  r.onFull(bytes.data(), bytes.size());
  sim::SimGfx frame(480, 800);
  const int band = statusBandHeight(frame);
  renderFrame(r, frame, false);
  const int rad = (band - 6) / 2, cx = 8 + rad, cy = band / 2;
  const int hintLeft = 480 - 8 - frame.textWidth(Font::Small, "Back: menu");
  bool bandClear = true;  // between the dot and the hint the band is empty: the template stays below it
  for (int y = 0; y < band; ++y)
    for (int x = cx + rad + 1; x < hintLeft; ++x) bandClear &= !frame.black(x, y);
  CHECK(bandClear);
  bool hintDrawn = false;
  for (int y = 0; y < band; ++y)
    for (int x = hintLeft; x < 480; ++x) hintDrawn |= frame.black(x, y);
  CHECK(hintDrawn);
  CHECK(frame.black(cx - rad, cy) && !frame.black(cx, cy));  // dark mode off: a hollow dot
  renderFrame(r, frame, bandDarkMode(gen::PhoneStatus{true, 7, false, {}}));
  CHECK(frame.black(cx - rad, cy) && frame.black(cx, cy));  // dark mode on: a filled dot
  CHECK(!bandDarkMode(std::nullopt));
}

static bool runGolden(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  sim::ReplayHooks hooks;
  hooks.fail = [&](const std::string& m) { std::printf("golden %s: %s\n", path.c_str(), m.c_str()); };
  return sim::replay(ss.str(), TemplateRegistry::instance(), hooks);
}

// Replays every golden transcript of every template directory (golden/<template>/*.txt).
static void testGoldens() {
  DIR* root = opendir(MIRROR_GOLDEN_DIR);
  CHECK(root != nullptr);
  int n = 0, templates = 0;
  while (root) {
    dirent* t = readdir(root);
    if (!t) break;
    if (t->d_name[0] == '.') continue;
    const std::string dir = std::string(MIRROR_GOLDEN_DIR) + "/" + t->d_name;
    DIR* d = opendir(dir.c_str());
    if (!d) continue;
    ++templates;
    while (dirent* e = readdir(d)) {
      const std::string name = e->d_name;
      if (name.size() <= 4 || name.substr(name.size() - 4) != ".txt") continue;
      ++n;
      const bool ok = runGolden(dir + "/" + name);
      std::printf("golden %s/%-24s %s\n", t->d_name, name.c_str(), ok ? "ok" : "FAILED");
      CHECK(ok);
    }
    closedir(d);
  }
  if (root) closedir(root);
  CHECK(templates >= 1 && n >= 8);
}

int main() {
  testCrcAndWire();
  testEnvelopeStream();
  testRefreshPolicy();
  testWrap();
  testChatRenderingAndDirtyRects();
  testRegistryAndCaps();
  testSessionAckSwitchAndDiffs();
  testSessionResyncCases();
  testStreamParserRejectsOversizedLengths();
  testWriterOverflowAndRegistry();
  testChatLayoutStates();
  testSessionErrors();
  testControlChannel();
  testDefaultChordAndDigits();
  testBandedGfxLeavesTheTopBandFree();
  testSessionButtons();
  testChatOpSemantics();
  testWrapHardBreakAndAlignment();
  testDraftGrowsAndFollowsCursor();
  testSingleOpDiffWithTrailingBytesLeavesTheStateAlone();
  testGoldens();
  std::printf("%d checks, %d failures\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
