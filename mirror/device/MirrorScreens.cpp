#if defined(CROSSPOINT_MIRROR)
// Unlock pad. Keys are sent the moment they are pressed and never stored.

#include "device/MirrorActivity.h"
#include "device/ScreenText.h"

namespace {
constexpr int kGridCols = 3;
constexpr uint8_t kEnterUsage = 0x28;
constexpr uint32_t kEnterAfterWakeMs = 400;

// PIN pad. The phone is woken when the screen opens, so there is no Wake key.
constexpr int kPinKeys = 12, kPinRows = 4;
const char* const kPinLabels[kPinKeys] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "Enter"};
enum PinKey { kPinBlank = 9, kPinZero = 10, kPinEnter = 11 };

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

bool MirrorActivity::sendKey(uint8_t usage) {
  const bool sent = ble_.typeKey(0, usage);
  note_ = sent ? "" : "Keyboard not ready: check the phone's Bluetooth";
  return sent;
}

void MirrorActivity::openUnlock() {
  setScreen(Screen::Unlock);
  if (ble_.wakePhone()) {  // before anything is typed
    // Enter brings up the PIN prompt on a locked phone, so the digits that follow have somewhere to go. Not
    // sent when the phone says it is already unlocked: Enter would reach whatever app is open. It waits a moment for
    // the screen to light up, without blocking the loop (see sendDueEnter).
    const auto& phone = session_->phoneStatus();
    if (!phone || phone->locked) enterDueMs_ = millis() + kEnterAfterWakeMs;
  }
  session_->sendCommand(mirror::gen::kCmdUnlockRequest);
}

void MirrorActivity::sendDueEnter() {
  if (enterDueMs_ == 0 || static_cast<int32_t>(millis() - enterDueMs_) < 0) return;
  enterDueMs_ = 0;
  if (screen_ == Screen::Unlock) sendKey(kEnterUsage);
}

// Moves the cursor with the arrow keys; true when it moved.
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
  const bool justUnlocked = phone && !phone->locked && link_.phoneWasLocked;
  link_.phoneWasLocked = phone ? phone->locked : true;
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
  if (press == kPinBlank) press = -1;
  if (press == kPinEnter)
    sendKey(kEnterUsage);
  else if (press >= 0)
    sendKey(mirror::digitUsage(press == kPinZero ? 0 : press + 1));
  return redraw | (press >= 0) | leaveWhenUnlocked();
}

void MirrorActivity::drawUnlock() {
  renderer.clearScreen();
  mirror::TextColumn text(renderer, renderer.getLineHeight(UI_10_FONT_ID) / 2);
  text.line("Unlock phone: PIN", true);
  const auto& phone = session_->phoneStatus();
  text.line(!phone ? "Phone state unknown" : phone->locked ? "Phone is locked" : "Phone is unlocked");
  // On touch boards no key is highlighted: e-ink would keep showing the last key after power-off.
  const bool showCursor = !mappedInput.hasTouch();
  for (int i = 0; i < kPinKeys; ++i) {
    const Cell c = gridCell(i, kPinRows, renderer.getScreenWidth(), renderer.getScreenHeight());
    const bool selected = showCursor && i == cursor_;
    const char* const label = kPinLabels[i];
    if (label[0] == '\0') continue;  // an empty slot in the grid
    renderer.drawRect(c.x, c.y, c.w, c.h, selected ? 4 : 1, true);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, label);
    renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      label, true, selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
  text.moveTo(renderer.getScreenHeight() - 3 * text.step());
  if (!note_.empty()) text.line(note_);
  text.line(mappedInput.hasTouch() ? "Tap the keys   Back: close" : "Arrows: move  Confirm: press  Back: close");
}

#endif
