#if defined(CROSSPOINT_MIRROR)
// Unlock pad, pattern and chord settings. Keys are sent the moment they are pressed and never stored.
#include <algorithm>
#include <cstdlib>

#include "device/MirrorActivity.h"
#include "device/MirrorSettings.h"
#include "device/ScreenText.h"

namespace {
constexpr int kGridCols = 3;
constexpr uint8_t kEnterUsage = 0x28;
constexpr uint32_t kEnterAfterWakeMs = 400;

// PIN pad. The phone is woken when the screen opens, so there is no Wake key.
constexpr int kPinKeys = 12, kPinRows = 4;
const char* const kPinLabels[kPinKeys] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "Pattern", "0", "Enter"};
enum PinKey { kPinToPattern = 9, kPinZero = 10, kPinEnter = 11 };

// Pattern: dots 0-8 row by row; the Android pattern lock reads them as the digits "0".."8". The finger drags over the
// dots; when it lifts, the dots it crossed are sent in order and then Enter. "Done" sends them on boards without touch.
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

// Chord settings rows.
enum ChordRow { kRowCtrl, kRowAlt, kRowShift, kRowKey, kRowSave, kRowCount };
struct ModifierRow {
  const char* name;
  uint8_t mask;
};
constexpr ModifierRow kModifierRows[] = {
    {"Ctrl", mirror::kModCtrl}, {"Alt", mirror::kModAlt}, {"Shift", mirror::kModShift}};
}  // namespace

void MirrorActivity::sendChordToPhone() { session_->sendCommand(mirror::gen::kCmdChord, chord_.mods, chord_.key); }

bool MirrorActivity::sendKey(uint8_t usage) {
  const bool allowed = phoneAllowsKeyboard();
  const bool sent = allowed && ble_.typeKey(0, usage);
  note_ = sent      ? ""
          : allowed ? "Keyboard not ready: check the phone's Bluetooth"
                    : "Keyboard is off: enable it in the app";
  return sent;
}

void MirrorActivity::openUnlock() {
  setScreen(Screen::Unlock);
  if (phoneAllowsKeyboard() && ble_.wakePhone()) {  // before anything is typed
    // Enter brings up the PIN or pattern prompt on a locked phone, so the digits that follow have somewhere to go. Not
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
    // A finger dragging over the dots collects them; they are sent when it lifts. The path is not drawn: e-ink would
    // keep it visible.
    if (mappedInput.isScreenTouchHeld(x, y) || mappedInput.wasScreenTouchDown(x, y)) {
      trackPatternTouch(x, y);
    } else if (patternTouching_ || mappedInput.wasScreenTouchReleased()) {
      patternTouching_ = false;
      finishPattern();
    }
    if (mappedInput.wasScreenTapped(x, y)) {
      const int hit = cellAt(x, y, kPatternKeys, kPatternRows, w, h);
      if (hit == kPatternToPin) press = cursor_ = hit;  // dots are only dragged; there is no Done key on touch boards
    }
  }
  if (press == kPatternToPin) {
    setScreen(Screen::Unlock);
    return true;
  }
  if (press == kPatternDone)
    finishPattern();
  else if (press >= 0 && press < kDots)
    visitDot(press);
  return redraw | (press >= 0) | leaveWhenUnlocked();
}

// Adds dot 0-8 to the path unless it is already part of it.
void MirrorActivity::visitDot(int dot) {
  if (dot < 0 || dot >= kDots || patternLen_ >= kDots) return;
  for (uint8_t i = 0; i < patternLen_; ++i)
    if (pattern_[i] == dot) return;
  pattern_[patternLen_++] = static_cast<uint8_t>(dot);
}

// The touch samples can be far apart (the loop is slow while the panel refreshes), so the straight line from the last
// sample is walked in small steps and every dot on it counts.
void MirrorActivity::trackPatternTouch(int x, int y) {
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const Cell cell = gridCell(0, kPatternRows, w, h);
  const int fromX = patternTouching_ ? patternX_ : x, fromY = patternTouching_ ? patternY_ : y;
  const int pixelStep = std::max(8, std::min(cell.w, cell.h) / 4);
  const int steps = std::max(1, std::max(std::abs(x - fromX), std::abs(y - fromY)) / pixelStep);
  for (int i = 1; i <= steps; ++i)
    visitDot(cellAt(fromX + (x - fromX) * i / steps, fromY + (y - fromY) * i / steps, kDots, kPatternRows, w, h));
  if (!patternTouching_) visitDot(cellAt(x, y, kDots, kPatternRows, w, h));
  patternTouching_ = true;
  patternX_ = x;
  patternY_ = y;
}

// Sends the collected dots in order, then Enter.
void MirrorActivity::finishPattern() {
  const uint8_t count = patternLen_;
  patternLen_ = 0;
  for (uint8_t i = 0; i < count; ++i)
    if (!sendKey(mirror::digitUsage(pattern_[i]))) return;
  if (count) sendKey(kEnterUsage);
}

// The chord rows as buttons: three modifiers, the key, Save.
void MirrorActivity::buildChordButtons() {
  buttons_.clear();
  for (const ModifierRow& m : kModifierRows)
    buttons_.add(std::string(m.name) + ": " + ((editChord_.mods & m.mask) ? "on" : "off"), buttons_.count());
  buttons_.add("Key: " + mirror::chordLabel({0, editChord_.key}) + "  (tap or Left/Right)", kRowKey);
  buttons_.add("Save", kRowSave);
}

int MirrorActivity::chordButtonsTop() const { return renderer.getScreenHeight() / 3; }

bool MirrorActivity::loopChordSettings() {
  using Btn = MappedInputManager::Button;
  bool redraw = false;
  if (mappedInput.wasPressed(Btn::Left)) editChord_.key = mirror::nextChordKey(editChord_.key, -1), redraw = true;
  if (mappedInput.wasPressed(Btn::Right)) editChord_.key = mirror::nextChordKey(editChord_.key, 1), redraw = true;
  if (!mappedInput.hasTouch()) {
    if (mappedInput.wasPressed(Btn::Up)) cursor_ = (cursor_ + kRowCount - 1) % kRowCount, redraw = true;
    if (mappedInput.wasPressed(Btn::Down)) cursor_ = (cursor_ + 1) % kRowCount, redraw = true;
  }

  int row = mappedInput.wasReleased(Btn::Confirm) ? cursor_ : -1;
  int x = 0, y = 0;
  buildChordButtons();
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(x, y)) {
    const int hit = buttons_.hit(renderer, x, y, chordButtonsTop());
    if (hit >= 0) row = cursor_ = hit;
  }
  if (row < 0) return redraw;

  if (row < kRowKey) {
    editChord_.mods ^= kModifierRows[row].mask;
  } else if (row == kRowKey) {
    editChord_.key = mirror::nextChordKey(editChord_.key, 1);
  } else if (mirror::checkChord(editChord_) == mirror::ChordCheck::Ok) {
    chord_ = editChord_;
    mirror::settings::setChord(chord_);
    if (ble_.connected() && session_->phoneStatus()) sendChordToPhone();
    setScreen(Screen::Status);
    note_ = "Chord saved";
  }
  return true;
}

void MirrorActivity::drawKeyGrid(const char* title, const char* const* labels, int count, int rows) {
  renderer.clearScreen();
  mirror::TextColumn text(renderer, renderer.getLineHeight(UI_10_FONT_ID) / 2);
  text.line(title, true);
  const auto& phone = session_->phoneStatus();
  text.line(!phone ? "Phone state unknown" : phone->locked ? "Phone is locked" : "Phone is unlocked");
  // On touch boards no key is highlighted: e-ink would keep showing the last key after power-off.
  const bool showCursor = !mappedInput.hasTouch();
  for (int i = 0; i < count; ++i) {
    const Cell c = gridCell(i, rows, renderer.getScreenWidth(), renderer.getScreenHeight());
    const bool selected = showCursor && i == cursor_;
    if (screen_ == Screen::Pattern) {
      if (i == kPatternDone && !showCursor) continue;  // touch boards send the pattern when the finger lifts
      if (i < kDots) {                                 // a dot, not a key: the pattern is dragged
        const int r = selected ? 22 : 14;
        renderer.fillRect(c.x + c.w / 2 - r, c.y + c.h / 2 - r, 2 * r, 2 * r, true);
        continue;
      }
    }
    renderer.drawRect(c.x, c.y, c.w, c.h, selected ? 4 : 1, true);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
    renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      labels[i], true, selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
  text.moveTo(renderer.getScreenHeight() - 3 * text.step());
  if (!note_.empty()) text.line(note_);
  text.line(mappedInput.hasTouch()
                ? (screen_ == Screen::Pattern ? "Drag over the dots  Home / Back: close" : "Tap  Home / Back: close")
                : "Arrows: move  Confirm: press  Back: close");
}

void MirrorActivity::drawChordSettings() {
  renderer.clearScreen();
  mirror::TextColumn text(renderer, renderer.getScreenHeight() / 8);
  text.line("Trigger chord", true);
  text.line(mirror::chordLabel(editChord_), true);
  const mirror::ChordCheck check = mirror::checkChord(editChord_);
  text.line(check == mirror::ChordCheck::Ok ? "Sent from the Mirror screen to start the app on your phone"
                                            : mirror::chordCheckText(check));
  buildChordButtons();
  buttons_.draw(renderer, chordButtonsTop(), cursor_, !mappedInput.hasTouch());
  text.moveTo(renderer.getScreenHeight() - 2 * text.step());
  text.line("Home / Back: cancel");
}

void MirrorActivity::drawSubScreen() {
  if (screen_ == Screen::Unlock)
    drawKeyGrid("Unlock phone: PIN", kPinLabels, kPinKeys, kPinRows);
  else if (screen_ == Screen::Pattern)
    drawKeyGrid("Unlock phone: pattern", kPatternLabels, kPatternKeys, kPatternRows);
  else
    drawChordSettings();
}
#endif
