#if defined(CROSSPOINT_MIRROR)
// Unlock pad, pattern and chord settings. Keys are sent the moment they are pressed and never stored.
#include <algorithm>

#include "device/MirrorActivity.h"
#include "device/MirrorSettings.h"
#include "device/ScreenText.h"

namespace {
constexpr int kGridCols = 3;
constexpr uint8_t kEnterUsage = 0x28;

// PIN pad. The phone is woken when the screen opens, so there is no Wake key.
constexpr int kPinKeys = 12, kPinRows = 4;
const char* const kPinLabels[kPinKeys] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "Pattern", "0", "Enter"};
enum PinKey { kPinToPattern = 9, kPinZero = 10, kPinEnter = 11 };

// Pattern: dots 0-8 row by row; the Android pattern lock reads them as the digits "0".."8". Lifting the finger sends
// Enter; "Done" does the same on boards without touch.
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
  if (phoneAllowsKeyboard()) ble_.wakePhone();  // before anything is typed
  session_->sendCommand(mirror::gen::kCmdUnlockRequest);
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
    setScreen(Screen::Unlock);
    return true;
  }
  if (press == kPatternDone)
    finishPattern();
  else if (press >= 0)
    visitDot(press);
  return redraw | (press >= 0) | leaveWhenUnlocked();
}

// Sends dot 0-8 unless it is already part of this pattern.
void MirrorActivity::visitDot(int dot) {
  if (dot < 0 || dot >= kDots || (patternVisited_ & (1u << dot))) return;
  patternVisited_ |= 1u << dot;
  sendKey(mirror::digitUsage(dot));
}

void MirrorActivity::finishPattern() {
  if (patternVisited_) sendKey(kEnterUsage);
  patternVisited_ = 0;
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
    renderer.drawRect(c.x, c.y, c.w, c.h, selected ? 4 : 1, true);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
    renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + (c.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      labels[i], true, selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
  text.moveTo(renderer.getScreenHeight() - 3 * text.step());
  if (!note_.empty()) text.line(note_);
  text.line(mappedInput.hasTouch() ? "Tap or slide  Home / Back: close" : "Arrows: move  Confirm: press  Back: close");
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
