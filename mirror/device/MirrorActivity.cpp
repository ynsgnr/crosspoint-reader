#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorActivity.h"

#include <algorithm>
#include <cstdio>

#include "core/Frame.h"
#include "device/ScreenText.h"

namespace {
constexpr char kDeviceName[] = "E-Ink Reader";  // generic on purpose: the advertisement identifies nothing
constexpr uint32_t kPairingWindowMs = 120000;
constexpr int kPartialsBeforeFull = 10;
constexpr unsigned long kLongPressMs = 500;
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
  screen_ = drawnScreen_ = Screen::Status;
  armed_ = kNone;
  bondsForgotten_ = false;
  link_ = Link{};
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
  startup_ = ble_.start(kDeviceName, session_->capabilities(gfx_, kDeviceName, mirror::gen::kFeatureHidKeyboard));
}

void MirrorActivity::setScreen(Screen s) {
  screen_ = s;
  cursor_ = 0;
  armed_ = kNone;  // a second tap must follow the first on the same screen
  enterDueMs_ = 0;
  note_.clear();  // nothing typed is kept when a screen is left
}

bool MirrorActivity::showsChat() const { return screen_ == Screen::Status && session_ && session_->active(); }

void MirrorActivity::loop() {
  // Back goes up one level: unlock pad -> where it was opened, chat -> menu, menu or status page -> leave Mirror.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    bool leave = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);  // released before finish(): onExit takes it too
      if (screen_ == Screen::Unlock)
        setScreen(Screen::Status);
      else if (showsChat())
        setScreen(Screen::Menu);
      else
        leave = true;
    }
    return leave ? finish() : requestUpdate();
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
  sendDueEnter();
  switch (screen_) {
    case Screen::Status:
      redraw |= session_->active() ? loopChat() : loopStatus();
      break;
    case Screen::Menu:
      redraw |= loopStatus();
      break;
    case Screen::Unlock:
      redraw |= loopUnlock();
      break;
  }
  if (redraw) requestUpdate();
}

// Every connection starts a new sequence space; a disconnect returns to the status page.
void MirrorActivity::onLinkChanged() {
  link_ = Link{ble_.connected(), ble_.connectionEpoch()};
  armed_ = kNone;
  resetSession();
  if (link_.connected || screen_ == Screen::Status) return;
  std::lock_guard<std::mutex> lock(mutex_);
  setScreen(Screen::Status);
}

// The phone's first status on a connection means its app is up: tell it the HID state.
void MirrorActivity::announceKeyboardToPhone() {
  if (!ble_.connected() || !session_->phoneStatus()) return;
  if (ble_.keyboardReady() != link_.hidAnnounced) {
    link_.hidAnnounced = ble_.keyboardReady();
    session_->sendCommand(mirror::gen::kCmdHidStatus, link_.hidAnnounced ? 1 : 0);
  }
}

bool MirrorActivity::phoneLocked() const {
  const auto& phone = session_->phoneStatus();
  return phone && phone->locked;
}

// Status-page buttons. Touch boards tap them; boards with Confirm move a cursor.
void MirrorActivity::buildActions() {
  buttons_.clear();
  if (!ble_.connected()) {
    buttons_.add(ble_.pairingOpen() ? "Pairing open: restart" : "Pair phone", kPairPhone);
    if (ble_.bondCount() > 0)
      buttons_.add(armed_ == kForgetPhone ? "Tap again: forget phone" : "Forget phone", kForgetPhone);
    return;
  }
  const auto& phone = session_->phoneStatus();
  if (screen_ == Screen::Menu && session_->active()) buttons_.add("Back to chat", kBackToChat);
  if (!phone) buttons_.add("Start mirror on phone", kStartMirror);  // the phone's app is not linked yet
  if (phoneLocked()) buttons_.add("Unlock phone", kUnlock);
  if (phone)
    for (size_t i = 0; i < phone->apps.size(); ++i)
      buttons_.add(phone->apps[i].name, kLaunchApp + static_cast<int>(i), /*half=*/true);
}

int MirrorActivity::buttonsTop() const { return renderer.getScreenHeight() / 3; }

void MirrorActivity::runAction(int action) {
  if (action == kForgetPhone && armed_ != action) {  // it discards the pairing: ask once
    armed_ = action;
    return;
  }
  armed_ = kNone;
  if (action >= kLaunchApp) {
    const auto& phone = session_->phoneStatus();
    const size_t i = static_cast<size_t>(action - kLaunchApp);
    if (!phone || i >= phone->apps.size()) return;
    const std::string name = phone->apps[i].name;
    session_->sendCommand(mirror::gen::kCmdLaunchApp, static_cast<uint8_t>(i));
    setScreen(Screen::Status);  // back to the chat, which follows the app that opens
    note_ = "Opening " + name;
    return;
  }
  switch (action) {
    case kPairPhone:
      bondsForgotten_ = false;
      ble_.openPairing(kPairingWindowMs);  // the only way an unknown phone can connect
      break;
    case kForgetPhone:
      ble_.forgetBonds();
      bondsForgotten_ = true;
      break;
    case kStartMirror:  // the chord makes the phone start its app session
      note_ = ble_.typeKey(mirror::kDefaultChord.mods, mirror::kDefaultChord.key) ? "Start sent" : "Keyboard not ready";
      break;
    case kUnlock:
      openUnlock();
      break;
    case kBackToChat:
      setScreen(Screen::Status);
      break;
    default:
      break;
  }
}

bool MirrorActivity::loopStatus() {
  if (ble_.pairingPasskey() >= 0) return false;  // the code is on screen instead of the buttons: nothing to press
  using Btn = MappedInputManager::Button;
  buildActions();
  const int n = buttons_.count();
  if (n == 0) return false;
  bool redraw = false;
  int press = -1;
  int x = 0, y = 0;
  if (mappedInput.hasTouch()) {
    if (mappedInput.wasScreenTapped(x, y)) press = buttons_.hit(renderer, x, y, buttonsTop());
  } else {
    if (mappedInput.wasPressed(Btn::Down) || mappedInput.wasPressed(Btn::Right))
      cursor_ = (cursor_ + 1) % n, redraw = true;
    if (mappedInput.wasPressed(Btn::Up) || mappedInput.wasPressed(Btn::Left))
      cursor_ = (cursor_ + n - 1) % n, redraw = true;
    cursor_ = std::min(cursor_, n - 1);
    if (mappedInput.wasReleased(Btn::Confirm)) press = cursor_;
  }
  if (press >= 0) {
    const Screen before = screen_;
    runAction(buttons_.id(press));
    if (screen_ == before) cursor_ = std::min(press, std::max(0, buttons_.count() - 1));  // a new screen starts at 0
    redraw = true;
  }
  return redraw;
}

// The chat screen: scroll with the side buttons or a swipe; the top of the screen toggles the phone's dark mode.
bool MirrorActivity::loopChat() {
  using Btn = MappedInputManager::Button;
  constexpr int kDarkModeTapHeight = 72;
  int x = 0, y = 0;
  const bool tapTop = mappedInput.hasTouch() && mappedInput.wasScreenTapped(x, y) && y < kDarkModeTapHeight;
  if (tapTop || mappedInput.wasReleased(Btn::Confirm))
    session_->sendCommand(mirror::gen::kCmdToggleDarkMode);  // the new state comes back as a status message
  const auto swipe = mappedInput.wasSwipe();
  const bool up = mappedInput.wasPressed(Btn::Up) || mappedInput.wasPressed(Btn::Left) ||
                  mappedInput.wasPressed(Btn::PageBack) || swipe == MappedInputManager::SwipeDir::Down;
  const bool down = mappedInput.wasPressed(Btn::Down) || mappedInput.wasPressed(Btn::Right) ||
                    mappedInput.wasPressed(Btn::PageForward) || swipe == MappedInputManager::SwipeDir::Up;
  // Holding a button jumps to the oldest or newest message; a short press scrolls one message.
  const bool toTop = mappedInput.wasLongPressed(Btn::Up, kLongPressMs) ||
                     mappedInput.wasLongPressed(Btn::Left, kLongPressMs) ||
                     mappedInput.wasLongPressed(Btn::PageBack, kLongPressMs);
  const bool toBottom = mappedInput.wasLongPressed(Btn::Down, kLongPressMs) ||
                        mappedInput.wasLongPressed(Btn::Right, kLongPressMs) ||
                        mappedInput.wasLongPressed(Btn::PageForward, kLongPressMs);
  // The event goes to the phone, which moves through the messages it holds and, past the oldest, scrolls the chat app
  // itself for more; the new view comes back as a diff.
  if (toTop)
    session_->onButton(mirror::Button::Top);
  else if (toBottom)
    session_->onButton(mirror::Button::Bottom);
  else if (up)
    session_->onButton(mirror::Button::Up);
  else if (down)
    session_->onButton(mirror::Button::Down);
  return false;
}

void MirrorActivity::drawStatus() {
  renderer.clearScreen();
  mirror::TextColumn text(renderer, renderer.getScreenHeight() / 8);
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
  } else {
    const auto& phone = session_->phoneStatus();
    if (!ble_.connected()) {
      text.line(ble_.pairingOpen()         ? "Pairing open: Retink app > Pair the e-ink device"
                : ble_.bondCount() == 0    ? "No phone paired yet"
                : ble_.advertisingFailed() ? "Bluetooth advertising failed: leave and reopen"
                                           : "Waiting for your phone");
    } else if (!phone) {
      text.line("Connected. Waiting for the Retink app.");
    } else if (phone->locked) {
      text.line("Phone is locked");
    } else {
      text.line(phone->apps.empty() ? "Connected. Open a chat app on your phone." : "Connected. Open on your phone:");
    }
    if (bondsForgotten_) text.line("Forgotten: also remove it in the phone's Bluetooth settings");
    buildActions();
    buttons_.draw(renderer, buttonsTop(), cursor_, !mappedInput.hasTouch());
  }
  text.moveTo(renderer.getScreenHeight() - 3 * text.step());
  if (!note_.empty()) text.line(note_);
  // Visible without a serial cable: which firmware runs and whether the radio is out there.
  text.line(std::string(kDeviceName) + (ble_.advertising() ? ", advertising" : "") + ", build " + __DATE__);
  text.line("Back: leave Mirror");
}

mirror::RefreshKind MirrorActivity::drawFrame() {
  if (screen_ == Screen::Unlock) {
    drawUnlock();
    const bool full = drawnScreen_ != screen_ || ++subScreenPartials_ >= kPartialsBeforeFull;  // clears ghosting
    drawnScreen_ = screen_;
    statusShown_ = false;
    forceFull_ = true;  // the next chat or status frame redraws everything
    if (full) subScreenPartials_ = 0;
    return full ? mirror::RefreshKind::Full : mirror::RefreshKind::Partial;
  }
  drawnScreen_ = screen_;
  mirror::TemplateRenderer* t = showsChat() ? session_->active() : nullptr;
  if (!t) {
    drawStatus();
    const bool full = !statusShown_ || ++subScreenPartials_ >= kPartialsBeforeFull;  // clears the cursor's ghosting too
    statusShown_ = true;
    forceFull_ = true;
    if (full) subScreenPartials_ = 0;
    return full ? mirror::RefreshKind::Full : mirror::RefreshKind::Partial;
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
