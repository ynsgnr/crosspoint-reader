#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
#include "MacroActivity.h"

#include <cstdio>
#include <vector>

#include "MacroSettings.h"
#include "SilentRestart.h"
#include "device/ScreenText.h"

namespace {
constexpr char kDeviceName[] = "E-Ink Reader";  // the name Mirror advertises: same device, same bond
constexpr int kPartialsBeforeFull = 10;

struct Cell {
  int x, y, w, h;
};

Cell gridCell(int i, int screenW, int screenH) {
  const int top = screenH / 5, gap = 12;
  const int cellW = (screenW - gap * (macro::kMacroCols + 1)) / macro::kMacroCols;
  const int cellH = (screenH * 3 / 5 - gap * (macro::kMacroRows - 1)) / macro::kMacroRows;
  return {gap + (i % macro::kMacroCols) * (cellW + gap), top + (i / macro::kMacroCols) * (cellH + gap), cellW, cellH};
}

// The transport switch sits right below the grid.
Cell switchCell(int screenW, int screenH, int lineH) {
  const Cell last = gridCell(macro::kMacroCount - 1, screenW, screenH);
  const int gap = 12;
  return {gap, last.y + last.h + gap, screenW - 2 * gap, lineH + 16};
}

bool inside(const Cell& c, int x, int y) { return x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h; }

int cellAt(int x, int y, int screenW, int screenH) {
  for (int i = 0; i < macro::kMacroCount; ++i) {
    const Cell c = gridCell(i, screenW, screenH);
    if (inside(c, x, y)) return i;
  }
  return -1;
}
}  // namespace

void MacroActivity::onEnter() {
  Activity::onEnter();
  note_[0] = '\0';
  cursor_ = 0;
  fullNext_ = true;
  useBle_ = !canSwitch() || macro::settings::bleEnabled();  // USB unless Bluetooth is switched on
  startTransport();
  requestUpdate();
}

void MacroActivity::onExit() {
  ble_.stop();
  Activity::onExit();
}

// Brings up the active transport and stops Bluetooth when USB is active. TinyUSB stays up once started (see leave()).
void MacroActivity::startTransport() {
  connected_ = hidReady_ = false;
  if (!useBle_) {
#if MACRO_HAS_USB
    ble_.stop();
    start_ = usb_.start() ? Start::Ok : Start::Failed;
#endif
    return;
  }
  // The capabilities characteristic is the Mirror protocol's; the deck has no app session, so it is empty.
  switch (ble_.start(kDeviceName, std::vector<uint8_t>{})) {
    case mirror::MirrorBle::StartResult::Ok:
      start_ = Start::Ok;
      break;
    case mirror::MirrorBle::StartResult::NoMemory:
      start_ = Start::NoMemory;
      break;
    default:
      start_ = Start::Failed;
      break;
  }
}

void MacroActivity::toggleTransport() {
  useBle_ = !useBle_;
  macro::settings::setBleEnabled(useBle_);
  note_[0] = '\0';
  startTransport();
}

// Once TinyUSB has started it cannot be stopped, so leaving after that reboots to Home (and hands the USB PHY back).
void MacroActivity::leave() {
#if MACRO_HAS_USB
  if (usb_.started()) return restartToHomeAfterStorageHandoff();
#endif
  finish();
}

bool MacroActivity::linkUp() const {
  if (start_ != Start::Ok) return false;
#if MACRO_HAS_USB
  if (!useBle_) return usb_.connected();
#endif
  return ble_.connected();
}

bool MacroActivity::linkReady() const {
  if (!linkUp()) return false;
#if MACRO_HAS_USB
  if (!useBle_) return usb_.ready();
#endif
  return ble_.keyboardReady();
}

bool MacroActivity::moveCursor(bool gridShown) {
  using Btn = MappedInputManager::Button;
  int delta = 0;
  if (mappedInput.wasPressed(Btn::Left)) delta = -1;
  if (mappedInput.wasPressed(Btn::Right)) delta = 1;
  if (mappedInput.wasPressed(Btn::Up)) delta = -macro::kMacroCols;
  if (mappedInput.wasPressed(Btn::Down)) delta = macro::kMacroCols;
  if (delta == 0 || !gridShown) return false;
  const int slots = macro::kMacroCount + (canSwitch() ? 1 : 0);  // the keys, then the switch
  cursor_ = (cursor_ + delta + slots) % slots;
  return true;
}

void MacroActivity::sendMacro(int index) {
  const macro::Macro& m = macro::kMacros[index];
  bool sent;
#if MACRO_HAS_USB
  if (!useBle_)
    sent = m.consumer ? usb_.tapConsumer(m.consumer) : usb_.tapKey(m.mods, m.usage);
  else
#endif
    sent = m.consumer ? ble_.typeConsumer(m.consumer) : ble_.typeKey(m.mods, m.usage);
  if (sent)
    std::snprintf(note_, sizeof note_, "Sent: %s", m.label);
  else
    std::snprintf(note_, sizeof note_, "Not sent: host not ready");
}

void MacroActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) return leave();

  bool redraw = false;
  if (useBle_ && start_ == Start::Ok) {
    ble_.service();
    std::vector<uint8_t> unused;  // the Mirror characteristic can still receive bytes from a Retink phone: drop them
    bool overflowed = false;
    ble_.drain(unused, overflowed);
    redraw = ble_.takeStatusChanged();
  }
  const bool connected = linkUp(), hidReady = linkReady();
  if (connected != connected_ || hidReady != hidReady_) {
    connected_ = connected;
    hidReady_ = hidReady;
    redraw = true;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const bool gridShown = connected_;
  if (!gridShown && canSwitch()) cursor_ = toggleIndex();  // the switch is all there is to select
  redraw |= moveCursor(gridShown);
  int press = mappedInput.wasReleased(MappedInputManager::Button::Confirm) ? cursor_ : -1;
  int x = 0, y = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(x, y)) {
    const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
    const int hit = gridShown ? cellAt(x, y, w, h) : -1;
    if (hit >= 0) {
      press = cursor_ = hit;
    } else if (canSwitch() && inside(switchCell(w, h, renderer.getLineHeight(UI_10_FONT_ID)), x, y)) {
      press = cursor_ = toggleIndex();
    }
  }
  if (press == toggleIndex() && canSwitch()) {
    toggleTransport();
    redraw = true;
  } else if (press >= 0 && press < macro::kMacroCount && gridShown) {
    sendMacro(press);
    redraw = true;
  }
  if (redraw) requestUpdate();
}

void MacroActivity::render(RenderLock&&) {
  bool full;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    renderer.clearScreen();
    mirror::TextColumn text(renderer, renderer.getScreenHeight() / 8);
    text.line("Macro deck", true);
    text.gap();
    bool grid = false;
    if (start_ == Start::NoMemory) {
      text.line("Not enough free memory for Bluetooth.");
    } else if (start_ == Start::Failed) {
      text.line(useBle_ ? "Bluetooth could not start." : "USB could not start.");
    } else if (!useBle_) {
      if (!linkUp()) {
        text.line("Connect the USB cable to the host");
      } else {
        grid = true;
        text.line(hidReady_ ? "Keys go to the USB host" : "Host not ready: keyboard not open yet");
      }
    } else if (ble_.bondCount() == 0) {
      text.line("No host paired.");
      text.line("In Mirror: Pair phone.");
    } else if (!ble_.connected()) {
      text.line("Waiting for the paired host");
    } else {
      grid = true;
      text.line(hidReady_ ? "Keys go to the paired host" : "Host not ready: keyboard not enabled yet");
    }
    if (grid) {
      const bool showCursor = !mappedInput.hasTouch();  // e-ink would keep showing the last selection
      for (int i = 0; i < macro::kMacroCount; ++i) {
        const Cell c = gridCell(i, renderer.getScreenWidth(), renderer.getScreenHeight());
        const bool selected = showCursor && i == cursor_;
        const char* label = macro::kMacros[i].label;
        renderer.drawRect(c.x, c.y, c.w, c.h, selected ? 4 : 1, true);
        const int tw = renderer.getTextWidth(UI_10_FONT_ID, label);
        renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                          label, true, selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      }
    }
    if (canSwitch()) {
      const bool showCursor = !mappedInput.hasTouch();
      const bool selected = showCursor && cursor_ == toggleIndex();
      const Cell c =
          switchCell(renderer.getScreenWidth(), renderer.getScreenHeight(), renderer.getLineHeight(UI_10_FONT_ID));
      const char* label = useBle_ ? "Bluetooth: On" : "Bluetooth: Off (USB)";
      renderer.drawRect(c.x, c.y, c.w, c.h, selected ? 4 : 1, true);
      const int tw = renderer.getTextWidth(UI_10_FONT_ID, label);
      renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                        label, true, selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
    text.moveTo(renderer.getScreenHeight() - 3 * text.step());
    if (note_[0] != '\0') text.line(note_);
    if (grid)
      text.line(mappedInput.hasTouch() ? "Tap  Home / Back: close" : "Arrows: move  Confirm: send  Back: close");
    else if (canSwitch())
      text.line(mappedInput.hasTouch() ? "Tap the switch to change.  Back: close" : "Confirm: switch  Back: close");
    else
      text.line("Home / Back: close");
    full = fullNext_ || ++partials_ >= kPartialsBeforeFull;  // a full refresh clears ghosting
    fullNext_ = false;
    if (full) partials_ = 0;
  }
  renderer.displayBuffer(full ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
#endif
