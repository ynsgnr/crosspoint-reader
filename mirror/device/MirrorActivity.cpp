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
  switch (ble_.start(kDeviceName, session_->capabilities(gfx_, kDeviceName, features), keyboardOn_)) {
    case mirror::MirrorBle::StartResult::Ok:
      startup_ = Startup::Ok;
      break;
    case mirror::MirrorBle::StartResult::NoMemory:
      startup_ = Startup::NoMemory;
      break;
    default:
      startup_ = Startup::Failed;
      break;
  }
}

void MirrorActivity::setScreen(Screen s) {
  screen_ = s;
  cursor_ = 0;
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
    wasConnected_ = ble_.connected();
    seenEpoch_ = ble_.connectionEpoch();
    resetSession();  // every connection starts a new sequence space; a disconnect returns to the waiting screen
    hidReadyAnnounced_ = false;
    phoneWasLocked_ = true;
    if (!wasConnected_ && screen_ == Screen::Unlock) {
      std::lock_guard<std::mutex> lock(mutex_);
      setScreen(Screen::Status);
    }
    redraw = true;
  }

  std::vector<uint8_t> bytes;
  bool overflowed = false;
  const bool received = ble_.drain(bytes, overflowed);
  std::lock_guard<std::mutex> lock(mutex_);
  if (overflowed) session_->requestResync();
  if (received && session_->feed(bytes.data(), bytes.size())) redraw = true;

  // The phone's first status on a connection means its app is up: tell it the chord and the keyboard state.
  if (ble_.connected() && session_->phoneStatus()) {
    if (keyboardOn_ && chordSentEpoch_ != seenEpoch_) {
      chordSentEpoch_ = seenEpoch_;
      sendChordToPhone();
    }
    if (keyboardOn_ && ble_.keyboardReady() != hidReadyAnnounced_) {
      hidReadyAnnounced_ = ble_.keyboardReady();
      session_->sendCommand(mirror::gen::kCmdHidStatus, hidReadyAnnounced_ ? 1 : 0);
    }
  }

  switch (screen_) {
    case Screen::Unlock: redraw |= loopUnlock(); break;
    case Screen::ChordSettings: redraw |= loopChordSettings(); break;
    case Screen::Status: redraw |= loopStatus(); break;
  }
  if (redraw) requestUpdate();
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
      setScreen(Screen::Unlock);
      session_->sendCommand(mirror::gen::kCmdUnlockRequest);
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
constexpr int kUnlockCols = 3, kUnlockRows = 4, kUnlockKeys = 12;
const char* const kUnlockLabels[kUnlockKeys] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "Wake", "0", "Enter"};

struct Cell {
  int x, y, w, h;
};

Cell unlockCell(int i, int screenW, int screenH) {
  const int top = screenH / 5, gap = 12;
  const int cellW = (screenW - gap * (kUnlockCols + 1)) / kUnlockCols;
  const int cellH = (screenH * 3 / 5 - gap * (kUnlockRows - 1)) / kUnlockRows;
  return {gap + (i % kUnlockCols) * (cellW + gap), top + (i / kUnlockCols) * (cellH + gap), cellW, cellH};
}
}  // namespace

bool MirrorActivity::loopUnlock() {
  using Btn = MappedInputManager::Button;
  bool redraw = false;
  auto move = [&](int delta) {
    cursor_ = (cursor_ + delta + kUnlockKeys) % kUnlockKeys;
    redraw = true;
  };
  if (mappedInput.wasPressed(Btn::Left)) move(-1);
  if (mappedInput.wasPressed(Btn::Right)) move(1);
  if (mappedInput.wasPressed(Btn::Up)) move(-kUnlockCols);
  if (mappedInput.wasPressed(Btn::Down)) move(kUnlockCols);

  int press = -1;
  if (mappedInput.wasReleased(Btn::Confirm)) press = cursor_;
  int tx = 0, ty = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(tx, ty)) {
    for (int i = 0; i < kUnlockKeys; ++i) {
      const Cell c = unlockCell(i, renderer.getScreenWidth(), renderer.getScreenHeight());
      if (tx >= c.x && tx < c.x + c.w && ty >= c.y && ty < c.y + c.h) press = cursor_ = i;
    }
  }
  if (press >= 0) {
    // Each key goes out immediately and is forgotten: no PIN, no digit count, no buffer.
    bool sent = false;
    if (phoneAllowsKeyboard()) {
      if (press == 9) sent = ble_.wakePhone();
      else if (press == 11) sent = ble_.typeKey(0, 0x28);  // Enter
      else sent = ble_.typeKey(0, mirror::digitUsage(press == 10 ? 0 : press + 1));
    }
    note_ = sent ? "" : phoneAllowsKeyboard() ? "Keyboard not ready" : "Keyboard is off in the app";
    redraw = true;  // the pressed key is highlighted
  }

  // The phone reports when it unlocks: stop offering the numpad.
  const auto& phone = session_->phoneStatus();
  if (phone && !phone->locked && phoneWasLocked_) {
    setScreen(Screen::Status);
    note_ = "Phone unlocked";
    redraw = true;
  }
  phoneWasLocked_ = phone ? phone->locked : true;
  return redraw;
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
    if (cursor_ == 0) editChord_.mods ^= mirror::kModCtrl;
    else if (cursor_ == 1) editChord_.mods ^= mirror::kModAlt;
    else if (cursor_ == 2) editChord_.mods ^= mirror::kModShift;
    else if (cursor_ == 3) editChord_.key = mirror::nextChordKey(editChord_.key, 1);
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

void MirrorActivity::drawUnlock() {
  renderer.clearScreen();
  const int lh = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  int y = lh / 2;
  renderer.drawCenteredText(UI_10_FONT_ID, y, "Unlock phone", true, EpdFontFamily::BOLD);
  y += lh;
  const auto& phone = session_->phoneStatus();
  renderer.drawCenteredText(UI_10_FONT_ID, y, !phone ? "Phone state unknown" : phone->locked ? "Phone is locked" : "Phone is unlocked");
  for (int i = 0; i < 12; ++i) {
    const Cell c = unlockCell(i, renderer.getScreenWidth(), renderer.getScreenHeight());
    renderer.drawRect(c.x, c.y, c.w, c.h, i == cursor_ ? 4 : 1, true);
    const char* label = kUnlockLabels[i];
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, label);
    renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2, label,
                      true, i == cursor_ ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
  y = renderer.getScreenHeight() - 2 * lh;
  if (!note_.empty()) renderer.drawCenteredText(UI_10_FONT_ID, y - lh, note_.c_str());
  renderer.drawCenteredText(UI_10_FONT_ID, y, "Arrows: move  Confirm: press  Back: close");
}

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
                               std::string("Key: ") + mirror::chordLabel({0, editChord_.key}),
                               "Save"};
  for (int i = 0; i < 5; ++i) line((i == cursor_ ? "> " : "  ") + rows[i], i == cursor_);
  y += lh / 2;
  const mirror::ChordCheck check = mirror::checkChord(editChord_);
  line(check == mirror::ChordCheck::Ok ? "Sent when you press Confirm on the linked screen" : mirror::chordCheckText(check));
  y = renderer.getScreenHeight() - 2 * lh;
  line("Up/Down: row  Confirm: change  Back: discard");
}

mirror::RefreshKind MirrorActivity::drawFrame() {
  if (screen_ != Screen::Status) {
    if (screen_ == Screen::Unlock) drawUnlock();
    else drawChordSettings();
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
