#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
#include "MacroActivity.h"

#include <cstdio>
#include <vector>

#include "MacroSettings.h"
#include "SilentRestart.h"
#include "device/ScreenText.h"
#include "platform/UsbSerialJtagHandoff.h"

namespace {
constexpr char kDeviceName[] = "E-Ink Reader";  // the name Mirror advertises: same device, same bond
constexpr int kPartialsBeforeFull = 10;

struct Cell {
  int x, y, w, h;
};

// The keys tile the whole screen.
Cell gridCell(int i, int screenW, int screenH) {
  const int gap = 6;
  const int cellW = (screenW - gap * (macro::kMacroCols + 1)) / macro::kMacroCols;
  const int cellH = (screenH - gap * (macro::kMacroRowsMax + 1)) / macro::kMacroRowsMax;
  return {gap + (i % macro::kMacroCols) * (cellW + gap), gap + (i / macro::kMacroCols) * (cellH + gap), cellW, cellH};
}

// The transport switch, shown while there is no grid.
Cell switchCell(int screenW, int screenH, int lineH) {
  const int gap = 12;
  return {gap, screenH * 3 / 5, screenW - 2 * gap, lineH + 16};
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
  cursor_ = 0;
  fullNext_ = true;
  useBle_ = !canSwitch() || macro::settings::bleEnabled();  // USB unless Bluetooth is switched on
  startTransport();
  requestUpdate();
}

void MacroActivity::onExit() {
  ble_.stop();
  if (canSwitch()) macro::settings::setBleEnabled(false);  // the next visit starts on USB again
  reboot();                                                // every way out of Macro, not only Back and Home
  Activity::onExit();
}

// Brings up the active transport and stops Bluetooth when USB is active. TinyUSB stays up once started (see reboot()).
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
  startTransport();
}

void MacroActivity::leave() { finish(); }  // onExit() does the rest, whichever way Macro is left

// TinyUSB cannot be stopped, so after USB was used Macro ends with a reboot to Home. It detaches from the host first
// and hands the USB PHY back to the serial port, as USB Drive does.
void MacroActivity::reboot() {
#if MACRO_HAS_USB
  if (!usb_.started() || rebooting_) return;
  rebooting_ = true;
  usb_.detach();
  delay(20);
  restartToHomeAfterStorageHandoff();  // does not return, except while the device goes to sleep
  handoffUsbOtgToSerialJtag();         // sleep keeps the USB PHY setting through wake, so give it back now
#endif
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

bool MacroActivity::moveCursor() {
  using Btn = MappedInputManager::Button;
  int delta = 0;
  if (mappedInput.wasPressed(Btn::Left)) delta = -1;
  if (mappedInput.wasPressed(Btn::Right)) delta = 1;
  if (mappedInput.wasPressed(Btn::Up)) delta = -macro::kMacroCols;
  if (mappedInput.wasPressed(Btn::Down)) delta = macro::kMacroCols;
  if (delta == 0) return false;
  cursor_ = (cursor_ + delta + macro::kMacroCount) % macro::kMacroCount;
  return true;
}

void MacroActivity::sendMacro(int index) {
  const macro::Macro& m = macro::kMacros[index];
#if MACRO_HAS_USB
  if (!useBle_) return void(m.consumer ? usb_.tapConsumer(m.consumer) : usb_.tapKey(m.mods, m.usage));
#endif
  m.consumer ? ble_.typeConsumer(m.consumer) : ble_.typeKey(m.mods, m.usage);
}

// Draws the key black, sends it, then draws it normal again.
void MacroActivity::press(int index) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pressed_ = index;
  }
  requestUpdateAndWait();
  sendMacro(index);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pressed_ = -1;
  }
  requestUpdate();
}

void MacroActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) return leave();

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

  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const bool touch = mappedInput.hasTouch();
  int x = 0, y = 0;
  int send = -1;
  bool toggle = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
      redraw |= moveCursor();
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) send = cursor_;
      if (touch) {
        if (mappedInput.wasScreenTouchDown(x, y)) {
          const int hit = cellAt(x, y, w, h);
          if (hit != pressed_) redraw = true;
          pressed_ = hit;  // black while the finger is down
        }
        if (mappedInput.wasScreenTapped(x, y)) {
          send = cellAt(x, y, w, h);
          if (send >= 0) cursor_ = send;
        }
        if (mappedInput.wasScreenTouchReleased() && send < 0 && pressed_ >= 0) {
          pressed_ = -1;  // the finger left the key without a tap
          redraw = true;
        }
      }
    } else if (canSwitch()) {
      toggle = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
      if (touch && mappedInput.wasScreenTapped(x, y))
        toggle = inside(switchCell(w, h, renderer.getLineHeight(UI_10_FONT_ID)), x, y);
    }
  }
  if (toggle) {
    toggleTransport();
    redraw = true;
  }
  if (send >= 0) return press(send);
  if (redraw) requestUpdate();
}

void MacroActivity::render(RenderLock&&) {
  bool full;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    renderer.clearScreen();
    const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
    if (connected_) {
      const bool showCursor = !mappedInput.hasTouch();  // e-ink would keep showing the last selection
      for (int i = 0; i < macro::kMacroCount; ++i) {
        const Cell c = gridCell(i, w, h);
        const bool down = i == pressed_, selected = showCursor && i == cursor_;
        const char* label = macro::kMacros[i].label;
        if (down)
          renderer.fillRect(c.x, c.y, c.w, c.h, true);
        else
          renderer.drawRect(c.x, c.y, c.w, c.h, selected ? 4 : 1, true);
        const int tw = renderer.getTextWidth(UI_12_FONT_ID, label);
        renderer.drawText(UI_12_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                          label, !down, selected || down ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      }
    } else {
      mirror::TextColumn text(renderer, h / 5);
      if (start_ == Start::NoMemory) {
        text.line("Not enough free memory for Bluetooth.");
      } else if (start_ == Start::Failed) {
        text.line(useBle_ ? "Bluetooth could not start." : "USB could not start.");
      } else if (!useBle_) {
        text.line("Connect the USB cable to the host");
      } else if (ble_.bondCount() == 0) {
        text.line("No host paired.");
        text.line("In Mirror: Pair phone.");
      } else {
        text.line("Waiting for the paired host");
      }
      if (canSwitch()) {
        const Cell c = switchCell(w, h, renderer.getLineHeight(UI_10_FONT_ID));
        const char* label = useBle_ ? "Bluetooth: On" : "Bluetooth: Off (USB)";
        renderer.drawRect(c.x, c.y, c.w, c.h, mappedInput.hasTouch() ? 1 : 4, true);
        const int tw = renderer.getTextWidth(UI_10_FONT_ID, label);
        renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                          label, true, EpdFontFamily::BOLD);
      }
    }
    full = fullNext_ || ++partials_ >= kPartialsBeforeFull;  // a full refresh clears ghosting
    fullNext_ = false;
    if (full) partials_ = 0;
  }
  renderer.displayBuffer(full ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
#endif
