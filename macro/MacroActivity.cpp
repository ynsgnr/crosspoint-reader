#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
#include "MacroActivity.h"

#include <cstdio>
#include <vector>

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

int cellAt(int x, int y, int screenW, int screenH) {
  for (int i = 0; i < macro::kMacroCount; ++i) {
    const Cell c = gridCell(i, screenW, screenH);
    if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return i;
  }
  return -1;
}
}  // namespace

void MacroActivity::onEnter() {
  Activity::onEnter();
  note_[0] = '\0';
  cursor_ = 0;
  fullNext_ = true;
  connected_ = hidReady_ = false;
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
  requestUpdate();
}

void MacroActivity::onExit() {
  ble_.stop();
  Activity::onExit();
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
  const bool sent = m.consumer ? ble_.typeConsumer(m.consumer) : ble_.typeKey(m.mods, m.usage);
  if (sent)
    std::snprintf(note_, sizeof note_, "Sent: %s", m.label);
  else
    std::snprintf(note_, sizeof note_, "Not sent: host not ready");
}

void MacroActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) return finish();
  if (start_ != Start::Ok) return;

  ble_.service();
  std::vector<uint8_t> unused;  // the Mirror characteristic can still receive bytes from a Retink phone: drop them
  bool overflowed = false;
  ble_.drain(unused, overflowed);

  bool redraw = ble_.takeStatusChanged();
  const bool connected = ble_.connected(), hidReady = ble_.keyboardReady();
  if (connected != connected_ || hidReady != hidReady_) {
    connected_ = connected;
    hidReady_ = hidReady;
    redraw = true;
  }
  if (!connected) {
    if (redraw) requestUpdate();
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  redraw |= moveCursor();
  int press = mappedInput.wasReleased(MappedInputManager::Button::Confirm) ? cursor_ : -1;
  int x = 0, y = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(x, y)) {
    const int hit = cellAt(x, y, renderer.getScreenWidth(), renderer.getScreenHeight());
    if (hit >= 0) press = cursor_ = hit;
  }
  if (press >= 0) {
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
      text.line("Bluetooth could not start.");
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
    text.moveTo(renderer.getScreenHeight() - 3 * text.step());
    if (note_[0] != '\0') text.line(note_);
    text.line(grid ? (mappedInput.hasTouch() ? "Tap  Home / Back: close" : "Arrows: move  Confirm: send  Back: close")
                   : "Home / Back: close");
    full = fullNext_ || ++partials_ >= kPartialsBeforeFull;  // a full refresh clears ghosting
    fullNext_ = false;
    if (full) partials_ = 0;
  }
  renderer.displayBuffer(full ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
#endif
