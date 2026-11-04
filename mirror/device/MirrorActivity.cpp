#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorActivity.h"

#include <cstdio>

#include "core/Frame.h"
#include "device/MirrorSettings.h"
#include "device/ScreenText.h"

namespace {
constexpr char kDeviceName[] = "E-Ink Reader";  // generic on purpose: the advertisement identifies nothing
constexpr uint32_t kPairingWindowMs = 120000;
constexpr unsigned long kHoldMs = 1500;
constexpr int kPartialsBeforeFull = 10;
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

void MirrorActivity::onExit() {
  note_.clear();
  ble_.stop();
  {
    std::lock_guard<std::mutex> g(mutex_);
    session_.reset();
  }
  Activity::onExit();
}

void MirrorActivity::startBle() {
  const uint8_t features = keyboardOn_ ? mirror::gen::kFeatureHidKeyboard : 0;
  startup_ = ble_.start(kDeviceName, session_->capabilities(gfx_, kDeviceName, features), keyboardOn_);
}

void MirrorActivity::setScreen(Screen s) {
  screen_ = s;
  cursor_ = 0;
  patternVisited_ = 0;
  note_.clear();  // nothing typed is kept when a screen is left
  if (s == Screen::ChordSettings) editChord_ = chord_;
}

void MirrorActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (screen_ == Screen::Status) return finish();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      setScreen(Screen::Status);
    }
    return requestUpdate();
  }
  if (startup_ != mirror::MirrorBle::StartResult::Ok) return;

  ble_.service();
  bool redraw = ble_.takeStatusChanged();
  if (ble_.connected() != link_.connected || ble_.connectionEpoch() != link_.epoch) {
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
    case Screen::Status:
      redraw |= loopStatus();
      break;
    case Screen::Unlock:
      redraw |= loopUnlock();
      break;
    case Screen::Pattern:
      redraw |= loopPattern();
      break;
    case Screen::ChordSettings:
      redraw |= loopChordSettings();
      break;
  }
  if (redraw) requestUpdate();
}

// Every connection starts a new sequence space; a disconnect returns to the waiting screen.
void MirrorActivity::onLinkChanged() {
  link_ = Link{ble_.connected(), ble_.connectionEpoch()};
  resetSession();
  if (link_.connected || (screen_ != Screen::Unlock && screen_ != Screen::Pattern)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  setScreen(Screen::Status);
}

// The phone's first status on a connection means its app is up: tell it the chord and the HID state.
void MirrorActivity::announceKeyboardToPhone() {
  if (!keyboardOn_ || !ble_.connected() || !session_->phoneStatus()) return;
  if (!link_.chordAnnounced) {
    link_.chordAnnounced = true;
    sendChordToPhone();
  }
  if (ble_.keyboardReady() != link_.hidAnnounced) {
    link_.hidAnnounced = ble_.keyboardReady();
    session_->sendCommand(mirror::gen::kCmdHidStatus, link_.hidAnnounced ? 1 : 0);
  }
}

bool MirrorActivity::phoneAllowsKeyboard() const {
  const auto& status = session_->phoneStatus();
  return !status || status->keyboard_allowed;  // unknown (no app session yet) counts as allowed
}

bool MirrorActivity::loopStatus() {
  using Btn = MappedInputManager::Button;
  bool redraw = false;
  if (!ble_.connected()) {
    if (mappedInput.wasLongPressed(Btn::Confirm, kHoldMs)) {
      ble_.forgetBonds();
      bondsForgotten_ = true;
      redraw = true;
    } else if (mappedInput.wasReleased(Btn::Confirm) && !ble_.pairingOpen()) {
      bondsForgotten_ = false;
      ble_.openPairing(kPairingWindowMs);  // "Pair phone": the only way an unknown phone can connect
      redraw = true;
    } else if (mappedInput.wasLongPressed(Btn::Left, kHoldMs)) {
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
    if (keyboardOn_ && mappedInput.wasLongPressed(Btn::Left, kHoldMs)) {
      openUnlock();
      redraw = true;
    }
  }
  if (mappedInput.wasLongPressed(Btn::Right, kHoldMs)) {
    setScreen(Screen::ChordSettings);
    redraw = true;
  }
  // The event goes to the phone; the new scroll position comes back as a diff.
  if (mappedInput.wasPressed(Btn::Up) || mappedInput.wasPressed(Btn::PageBack)) session_->onButton(mirror::Button::Up);
  if (mappedInput.wasPressed(Btn::Down) || mappedInput.wasPressed(Btn::PageForward))
    session_->onButton(mirror::Button::Down);
  return redraw;
}

void MirrorActivity::drawStatus() {
  renderer.clearScreen();
  mirror::TextColumn text(renderer, renderer.getScreenHeight() / 4);
  text.line("Mirror", true);
  text.gap();
  using Start = mirror::MirrorBle::StartResult;
  if (startup_ == Start::NoMemory) {
    text.line("Not enough free memory for Bluetooth.");
    text.line("Go back and try again.");
  } else if (startup_ == Start::Failed) {
    text.line("Bluetooth could not start.");
  } else if (const int32_t code = ble_.pairingPasskey(); code >= 0) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "Pairing code: %06d", static_cast<int>(code));
    text.line(buf, true);
    text.line("Type it on your phone.");
  } else if (ble_.connected()) {
    text.line("Connected.");
    text.line("Open a chat app on your phone.");
    text.gap();
    text.line("Confirm: phone dark mode on/off");
    if (keyboardOn_) {
      text.line("Hold Left: unlock phone");
      text.line("Confirm before the app runs: start mirror");
    }
    text.line("Hold Right: trigger chord");
  } else {
    const int bonds = ble_.bondCount();
    text.line(ble_.pairingOpen() ? "Pairing open: Retink app > Pair e-ink device"
              : bonds            ? "Waiting for your phone"
                                 : "No phone paired");
    text.line(std::string("Name: ") + kDeviceName);
    text.gap();
    if (!ble_.pairingOpen()) text.line("Confirm: pair phone");
    if (bondsForgotten_)
      text.line("Phone forgotten: also remove it in its Bluetooth settings");
    else if (bonds)
      text.line("Hold Confirm: forget phone");
    text.line(keyboardOn_ ? "Keyboard: on (hold Left: off)" : "Keyboard: off (hold Left: on)");
    text.line("Switching it forgets the phone.");
    text.line("Hold Right: trigger chord");
  }
  text.moveTo(renderer.getScreenHeight() - 3 * text.step());
  if (!note_.empty()) text.line(note_);
  text.line("Back: leave Mirror");
}

mirror::RefreshKind MirrorActivity::drawFrame() {
  if (screen_ != Screen::Status) {
    drawSubScreen();
    const bool full = drawnScreen_ != screen_ || ++subScreenPartials_ >= kPartialsBeforeFull;  // clears ghosting
    drawnScreen_ = screen_;
    statusShown_ = false;
    forceFull_ = true;  // the next chat or status frame redraws everything
    if (full) subScreenPartials_ = 0;
    return full ? mirror::RefreshKind::Full : mirror::RefreshKind::Partial;
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
