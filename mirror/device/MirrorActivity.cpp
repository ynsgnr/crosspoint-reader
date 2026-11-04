#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorActivity.h"

#include <HalDisplay.h>
#include <Logging.h>

#include "core/Frame.h"
#include "device/MirrorSettings.h"
#include "fontIds.h"

namespace {
constexpr char kDeviceName[] = "E-Ink Reader";  // generic on purpose: the advertisement identifies nothing
constexpr uint32_t kPairingWindowMs = 120000;
}  // namespace

void MirrorActivity::resetSession() {
  std::lock_guard<std::mutex> g(mutex_);
  session_ = std::make_unique<mirror::Session>(
      mirror::TemplateRegistry::instance(), [this](const std::vector<uint8_t>& frame) { ble_.send(frame); },
      kMaxPayload);
  forceFull_ = true;
  statusShown_ = false;
}

void MirrorActivity::onEnter() {
  Activity::onEnter();
  keyboardOn_ = mirror::settings::keyboardEnabled();
  chord_ = mirror::settings::chord();
  screen_ = drawnScreen_ = Screen::Status;
  // The page turner's radio is already off: it stops before every screen change and stays off outside readers.
  resetSession();
  startBle();
  requestUpdate();
}

void MirrorActivity::startBle() {
  const uint8_t features = keyboardOn_ ? mirror::gen::kFeatureHidKeyboard : 0;
  const auto result = ble_.start(kDeviceName, session_->capabilities(gfx_, kDeviceName, features), keyboardOn_);
  startup_ = result == mirror::MirrorBle::StartResult::Ok         ? Startup::Ok
             : result == mirror::MirrorBle::StartResult::NoMemory ? Startup::NoMemory
                                                                  : Startup::Failed;
}

void MirrorActivity::setScreen(Screen s) {
  screen_ = s;
  cursor_ = 0;
  patternVisited_ = 0;
  note_.clear();  // nothing typed is kept when a screen is left
  if (s == Screen::ChordSettings) editChord_ = chord_;
}

void MirrorActivity::onExit() {
  note_.clear();
  ble_.stop();
  {
    std::lock_guard<std::mutex> g(mutex_);
    session_.reset();
  }
  Activity::onExit();
}

void MirrorActivity::loop() {
  using Btn = MappedInputManager::Button;
  if (mappedInput.wasReleased(Btn::Back)) {
    if (screen_ == Screen::Status) return finish();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      setScreen(Screen::Status);
    }
    return requestUpdate();
  }
  if (startup_ != Startup::Ok) return;

  ble_.service();
  bool redraw = ble_.takeStatusChanged();
  if (ble_.connected() != wasConnected_ || ble_.connectionEpoch() != seenEpoch_) {
    onLinkChanged();
    redraw = true;
  }

  std::vector<uint8_t> bytes;
  bool overflowed = false;
  const bool received = ble_.drain(bytes, overflowed);
  std::lock_guard<std::mutex> lock(mutex_);
  if (overflowed) session_->requestResync();
  if (received && session_->feed(bytes.data(), bytes.size())) redraw = true;

  announceKeyboardToPhone();

  switch (screen_) {
    case Screen::Unlock:
      redraw |= loopUnlock();
      break;
    case Screen::Pattern:
      redraw |= loopPattern();
      break;
    case Screen::ChordSettings:
      redraw |= loopChordSettings();
      break;
    case Screen::Status:
      redraw |= loopStatus();
      break;
  }
  if (redraw) requestUpdate();
}

// Every connection starts a new sequence space; a disconnect returns to the waiting screen.
void MirrorActivity::onLinkChanged() {
  wasConnected_ = ble_.connected();
  seenEpoch_ = ble_.connectionEpoch();
  resetSession();
  hidReadyAnnounced_ = false;
  phoneWasLocked_ = true;
  if (wasConnected_ || (screen_ != Screen::Unlock && screen_ != Screen::Pattern)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  setScreen(Screen::Status);
}

// The phone's first status on a connection means its app is up: tell it the chord and the HID state.
void MirrorActivity::announceKeyboardToPhone() {
  if (!keyboardOn_ || !ble_.connected() || !session_->phoneStatus()) return;
  if (chordSentEpoch_ != seenEpoch_) {
    chordSentEpoch_ = seenEpoch_;
    sendChordToPhone();
  }
  if (ble_.keyboardReady() != hidReadyAnnounced_) {
    hidReadyAnnounced_ = ble_.keyboardReady();
    session_->sendCommand(mirror::gen::kCmdHidStatus, hidReadyAnnounced_ ? 1 : 0);
  }
}

bool MirrorActivity::phoneAllowsKeyboard() const {
  const auto& status = session_->phoneStatus();
  return !status || status->keyboard_allowed;  // unknown (no app session yet) counts as allowed
}

void MirrorActivity::sendChordToPhone() { session_->sendCommand(mirror::gen::kCmdChord, chord_.mods, chord_.key); }

bool MirrorActivity::loopStatus() {
  using Btn = MappedInputManager::Button;
  bool redraw = false;
  if (!ble_.connected()) {
    if (mappedInput.wasLongPressed(Btn::Confirm, 1500)) {
      ble_.forgetBonds();
      bondsForgotten_ = true;
      redraw = true;
    } else if (mappedInput.wasReleased(Btn::Confirm) && !ble_.pairingOpen()) {
      bondsForgotten_ = false;
      ble_.openPairing(kPairingWindowMs);  // "Pair phone": the only way an unknown phone can connect
      redraw = true;
    } else if (mappedInput.wasLongPressed(Btn::Left, 1500)) {
      // The keyboard changes the GATT database, which the phone caches per bond: pair again after switching.
      keyboardOn_ = !keyboardOn_;
      mirror::settings::setKeyboardEnabled(keyboardOn_);
      ble_.forgetBonds();
      ble_.stop();
      startBle();
      bondsForgotten_ = true;
      redraw = true;
    }
  } else {
    if (mappedInput.wasReleased(Btn::Confirm)) {
      if (session_->phoneStatus() || session_->active()) {
        session_->sendCommand(mirror::gen::kCmdToggleDarkMode);  // the new state comes back as a status message
      } else if (keyboardOn_) {
        // No app session yet: the chord makes the phone start it.
        note_ = phoneAllowsKeyboard() && ble_.typeKey(chord_.mods, chord_.key) ? "Start sent" : "Keyboard not ready";
        redraw = true;
      }
    }
    if (keyboardOn_ && mappedInput.wasLongPressed(Btn::Left, 1500)) {
      openUnlock();
      redraw = true;
    }
  }
  if (mappedInput.wasLongPressed(Btn::Right, 1500)) {
    setScreen(Screen::ChordSettings);
    redraw = true;
  }
  // The event goes to the phone; the new scroll position comes back as a diff.
  if (mappedInput.wasPressed(Btn::Up) || mappedInput.wasPressed(Btn::PageBack)) session_->onButton(mirror::Button::Up);
  if (mappedInput.wasPressed(Btn::Down) || mappedInput.wasPressed(Btn::PageForward))
    session_->onButton(mirror::Button::Down);
  return redraw;
}

namespace {
constexpr int kGridCols = 3;
constexpr uint8_t kEnterUsage = 0x28;

// PIN pad. The phone is woken when the screen opens, so there is no Wake key.
constexpr int kPinKeys = 12, kPinRows = 4;
const char* const kPinLabels[kPinKeys] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "Pattern", "0", "Enter"};
enum PinKey { kPinToPattern = 9, kPinZero = 10, kPinEnter = 11 };

// Pattern: dots 0-8 row by row (the Android pattern lock reads them as the digits "0".."8"). Lifting the finger sends
// Enter; "Done" does the same for boards without touch.
constexpr int kDots = 9, kPatternKeys = 11, kPatternRows = 4;
const char* const kPatternLabels[kPatternKeys] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "Done", "PIN"};
enum PatternKey { kPatternDone = 9, kPatternToPin = 10 };

struct Cell {
  int x, y, w, h;
};

Cell gridCell(int i, int rows, int screenW, int screenH) {
  const int top = screenH / 5, gap = 12;
  const int cellW = (screenW - gap * (kGridCols + 1)) / kGridCols;
  const int cellH = (screenH * 3 / 5 - gap * (rows - 1)) / rows;
  return {gap + (i % kGridCols) * (cellW + gap), top + (i / kGridCols) * (cellH + gap), cellW, cellH};
}

int cellAt(int x, int y, int count, int rows, int screenW, int screenH) {
  for (int i = 0; i < count; ++i) {
    const Cell c = gridCell(i, rows, screenW, screenH);
    if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return i;
  }
  return -1;
}
}  // namespace

// Every key goes out at once and is forgotten: no PIN, no pattern, no length, no buffer.
bool MirrorActivity::sendKey(uint8_t usage) {
  const bool allowed = phoneAllowsKeyboard();
  const bool sent = allowed && ble_.typeKey(0, usage);
  note_ = sent ? "" : allowed ? "Keyboard not ready" : "Keyboard is off in the app";
  return sent;
}

void MirrorActivity::openUnlock() {
  setScreen(Screen::Unlock);
  if (phoneAllowsKeyboard()) ble_.wakePhone();  // before anything is typed
  session_->sendCommand(mirror::gen::kCmdUnlockRequest);
}

void MirrorActivity::finishPattern() {
  if (patternVisited_) sendKey(kEnterUsage);
  patternVisited_ = 0;
}

// Moves the cursor with the arrow keys; returns true when it moved.
bool MirrorActivity::moveCursor(int count) {
  using Btn = MappedInputManager::Button;
  int delta = 0;
  if (mappedInput.wasPressed(Btn::Left)) delta = -1;
  if (mappedInput.wasPressed(Btn::Right)) delta = 1;
  if (mappedInput.wasPressed(Btn::Up)) delta = -kGridCols;
  if (mappedInput.wasPressed(Btn::Down)) delta = kGridCols;
  if (delta == 0) return false;
  cursor_ = (cursor_ + delta + count) % count;
  return true;
}

// The phone reports when it unlocks: leave the unlock screens.
bool MirrorActivity::leaveWhenUnlocked() {
  const auto& phone = session_->phoneStatus();
  const bool justUnlocked = phone && !phone->locked && phoneWasLocked_;
  phoneWasLocked_ = phone ? phone->locked : true;
  if (!justUnlocked) return false;
  setScreen(Screen::Status);
  note_ = "Phone unlocked";
  return true;
}

bool MirrorActivity::loopUnlock() {
  bool redraw = moveCursor(kPinKeys);
  int press = mappedInput.wasReleased(MappedInputManager::Button::Confirm) ? cursor_ : -1;
  int x = 0, y = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(x, y)) {
    const int hit = cellAt(x, y, kPinKeys, kPinRows, renderer.getScreenWidth(), renderer.getScreenHeight());
    if (hit >= 0) press = cursor_ = hit;
  }
  if (press == kPinToPattern) {
    setScreen(Screen::Pattern);
    return true;
  }
  if (press == kPinEnter)
    sendKey(kEnterUsage);
  else if (press >= 0)
    sendKey(mirror::digitUsage(press == kPinZero ? 0 : press + 1));
  return redraw | (press >= 0) | leaveWhenUnlocked();
}

bool MirrorActivity::loopPattern() {
  bool redraw = moveCursor(kPatternKeys);
  int press = mappedInput.wasReleased(MappedInputManager::Button::Confirm) ? cursor_ : -1;
  int x = 0, y = 0;
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  if (mappedInput.hasTouch()) {
    // A finger sliding over the dots sends each one once. The path is not drawn: e-ink would keep it visible.
    if (mappedInput.wasScreenTouchDown(x, y) || mappedInput.isScreenTouchHeld(x, y))
      visitDot(cellAt(x, y, kDots, kPatternRows, w, h));
    if (mappedInput.wasScreenTouchReleased()) finishPattern();
    if (mappedInput.wasScreenTapped(x, y)) {
      const int hit = cellAt(x, y, kPatternKeys, kPatternRows, w, h);
      if (hit >= kDots) press = cursor_ = hit;  // dots are handled by the slide above
    }
  }
  if (press == kPatternToPin) {
    patternVisited_ = 0;
    setScreen(Screen::Unlock);
    return true;
  }
  if (press == kPatternDone)
    finishPattern();
  else if (press >= 0)
    visitDot(press);
  return redraw | (press >= 0) | leaveWhenUnlocked();
}

// Sends dot `dot` (0-8) unless it is already part of this pattern.
void MirrorActivity::visitDot(int dot) {
  if (dot < 0 || dot >= kDots || (patternVisited_ & (1u << dot))) return;
  patternVisited_ |= 1u << dot;
  sendKey(mirror::digitUsage(dot));
}

bool MirrorActivity::loopChordSettings() {
  using Btn = MappedInputManager::Button;
  constexpr int kRows = 5;  // Ctrl, Alt, Shift, Key, Save
  bool redraw = false;
  if (mappedInput.wasPressed(Btn::Up)) cursor_ = (cursor_ + kRows - 1) % kRows, redraw = true;
  if (mappedInput.wasPressed(Btn::Down)) cursor_ = (cursor_ + 1) % kRows, redraw = true;
  if (cursor_ == 3) {
    if (mappedInput.wasPressed(Btn::Left)) editChord_.key = mirror::nextChordKey(editChord_.key, -1), redraw = true;
    if (mappedInput.wasPressed(Btn::Right)) editChord_.key = mirror::nextChordKey(editChord_.key, 1), redraw = true;
  }
  if (mappedInput.wasReleased(Btn::Confirm)) {
    redraw = true;
    if (cursor_ == 0)
      editChord_.mods ^= mirror::kModCtrl;
    else if (cursor_ == 1)
      editChord_.mods ^= mirror::kModAlt;
    else if (cursor_ == 2)
      editChord_.mods ^= mirror::kModShift;
    else if (cursor_ == 3)
      editChord_.key = mirror::nextChordKey(editChord_.key, 1);
    else if (mirror::checkChord(editChord_) == mirror::ChordCheck::Ok) {
      chord_ = editChord_;
      mirror::settings::setChord(chord_);
      if (ble_.connected() && session_->phoneStatus()) sendChordToPhone();
      setScreen(Screen::Status);
      note_ = "Chord saved";
    }
  }
  return redraw;
}

void MirrorActivity::drawStatus() {
  renderer.clearScreen();
  int y = renderer.getScreenHeight() / 4;
  const int lh = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  auto line = [&](const std::string& s, bool bold = false) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, s.c_str(), true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += lh;
  };
  line("Mirror", true);
  y += lh / 2;
  switch (startup_) {
    case Startup::NoMemory:
      line("Not enough free memory for Bluetooth.");
      line("Go back and try again.");
      break;
    case Startup::Failed:
      line("Bluetooth could not start.");
      break;
    case Startup::Ok: {
      const int32_t code = ble_.pairingPasskey();
      if (code >= 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "Pairing code: %06d", static_cast<int>(code));
        line(buf, true);
        line("Type it on your phone.");
      } else if (ble_.connected()) {
        line("Connected.");
        line("Open a chat app on your phone.");
        y += lh / 2;
        line("Confirm: phone dark mode on/off");
        if (keyboardOn_) {
          line("Hold Left: unlock phone");
          line("Confirm before the app runs: start mirror");
        }
        line("Hold Right: trigger chord");
      } else {
        const int bonds = ble_.bondCount();
        line(ble_.pairingOpen() ? "Pairing: open on your phone" : bonds ? "Waiting for your phone" : "No phone paired");
        line(std::string("Name: ") + kDeviceName);
        y += lh / 2;
        if (!ble_.pairingOpen()) line("Confirm: pair phone");
        if (bondsForgotten_)
          line("Paired phone forgotten.");
        else if (bonds)
          line("Hold Confirm: forget phone");
        line(keyboardOn_ ? "Keyboard: on (hold Left: off)" : "Keyboard: off (hold Left: on)");
        line("Switching it forgets the phone.");
        line("Hold Right: trigger chord");
      }
      break;
    }
  }
  y = renderer.getScreenHeight() - 3 * lh;
  if (!note_.empty()) line(note_);
  line("Back: leave Mirror");
}

void MirrorActivity::drawKeyGrid(const char* title, const char* const* labels, int count, int rows) {
  renderer.clearScreen();
  const int lh = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  int y = lh / 2;
  renderer.drawCenteredText(UI_10_FONT_ID, y, title, true, EpdFontFamily::BOLD);
  y += lh;
  const auto& phone = session_->phoneStatus();
  renderer.drawCenteredText(UI_10_FONT_ID, y,
                            !phone          ? "Phone state unknown"
                            : phone->locked ? "Phone is locked"
                                            : "Phone is unlocked");
  for (int i = 0; i < count; ++i) {
    const Cell c = gridCell(i, rows, renderer.getScreenWidth(), renderer.getScreenHeight());
    renderer.drawRect(c.x, c.y, c.w, c.h, i == cursor_ ? 4 : 1, true);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
    renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      labels[i], true, i == cursor_ ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
  y = renderer.getScreenHeight() - 2 * lh;
  if (!note_.empty()) renderer.drawCenteredText(UI_10_FONT_ID, y - lh, note_.c_str());
  renderer.drawCenteredText(UI_10_FONT_ID, y, "Arrows: move  Confirm: press  Back: close");
}

void MirrorActivity::drawUnlock() { drawKeyGrid("Unlock phone: PIN", kPinLabels, kPinKeys, kPinRows); }

void MirrorActivity::drawPattern() { drawKeyGrid("Unlock phone: pattern", kPatternLabels, kPatternKeys, kPatternRows); }

void MirrorActivity::drawChordSettings() {
  renderer.clearScreen();
  const int lh = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  int y = renderer.getScreenHeight() / 5;
  auto line = [&](const std::string& s, bool bold = false) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, s.c_str(), true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += lh;
  };
  line("Trigger chord", true);
  line(mirror::chordLabel(editChord_), true);
  y += lh / 2;
  const std::string rows[5] = {std::string("Ctrl: ") + ((editChord_.mods & mirror::kModCtrl) ? "on" : "off"),
                               std::string("Alt: ") + ((editChord_.mods & mirror::kModAlt) ? "on" : "off"),
                               std::string("Shift: ") + ((editChord_.mods & mirror::kModShift) ? "on" : "off"),
                               std::string("Key: ") + mirror::chordLabel({0, editChord_.key}), "Save"};
  for (int i = 0; i < 5; ++i) line((i == cursor_ ? "> " : "  ") + rows[i], i == cursor_);
  y += lh / 2;
  const mirror::ChordCheck check = mirror::checkChord(editChord_);
  line(check == mirror::ChordCheck::Ok ? "Sent when you press Confirm on the linked screen"
                                       : mirror::chordCheckText(check));
  y = renderer.getScreenHeight() - 2 * lh;
  line("Up/Down: row  Confirm: change  Back: discard");
}

mirror::RefreshKind MirrorActivity::drawFrame() {
  if (screen_ != Screen::Status) {
    if (screen_ == Screen::Unlock)
      drawUnlock();
    else if (screen_ == Screen::Pattern)
      drawPattern();
    else
      drawChordSettings();
    const bool first = drawnScreen_ != screen_;
    drawnScreen_ = screen_;
    statusShown_ = false;
    forceFull_ = true;  // the next chat or status frame redraws everything
    return first ? mirror::RefreshKind::Full : mirror::RefreshKind::Partial;
  }
  drawnScreen_ = Screen::Status;
  mirror::TemplateRenderer* t = session_ ? session_->active() : nullptr;
  if (!t) {
    drawStatus();
    const bool first = !statusShown_;
    statusShown_ = true;
    forceFull_ = true;
    return first ? mirror::RefreshKind::Full : mirror::RefreshKind::Partial;
  }
  statusShown_ = false;
  forceFull_ |= session_->takeTemplateSwitched();
  forceFull_ |= session_->takeResyncHappened();
  const std::string text = mirror::statusBandText(session_->phoneStatus());
  const mirror::DirtyRects dirty = mirror::renderFrame(*t, gfx_, text);
  const mirror::RefreshPlan plan = policy_.plan(dirty, gfx_.width(), gfx_.height() - mirror::statusBandHeight(gfx_),
                                                t->refreshOverrides(), forceFull_);
  forceFull_ = false;
  const bool bandChanged = text != drawnBand_;
  drawnBand_ = text;
  return plan.kind == mirror::RefreshKind::None && bandChanged ? mirror::RefreshKind::Partial : plan.kind;
}

void MirrorActivity::render(RenderLock&&) {
  mirror::RefreshKind kind;
  {
    std::lock_guard<std::mutex> lock(mutex_);  // not held during the refresh: it blocks for hundreds of ms
    kind = drawFrame();
  }
  // FAST_REFRESH only drives changed pixels, so a typed letter does not flash the page.
  if (kind == mirror::RefreshKind::Partial) renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  if (kind == mirror::RefreshKind::Full) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
#endif
