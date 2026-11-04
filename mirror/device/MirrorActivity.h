#pragma once
#if defined(CROSSPOINT_MIRROR)
// Mirror screen: BLE advertising and the connection exist only while it is open.
#include <memory>
#include <mutex>

#include "activities/Activity.h"
#include "core/Chord.h"
#include "core/RefreshPolicy.h"
#include "core/Session.h"
#include "device/GfxAdapter.h"
#include "device/MirrorBle.h"

class MirrorActivity final : public Activity {
 public:
  MirrorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Mirror", renderer, mappedInput), gfx_(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Keep the screen awake only while a phone is connected.
  bool preventAutoSleep() override { return ble_.connected(); }

 private:
  enum class Screen { Status, Unlock, Pattern, ChordSettings };

  void resetSession();
  void startBle();
  void setScreen(Screen s);
  // Per-screen input; each returns true when the screen needs a redraw. Called with mutex_ held.
  bool loopStatus();
  bool loopUnlock();
  bool loopPattern();
  bool sendKey(uint8_t usage);  // false when the keyboard is not ready
  void openUnlock();
  void visitDot(int dot);
  void finishPattern();
  bool moveCursor(int count);
  bool leaveWhenUnlocked();
  bool loopChordSettings();
  void onLinkChanged();
  void announceKeyboardToPhone();
  bool phoneAllowsKeyboard() const;
  void sendChordToPhone();
  void drawStatus();
  void drawUnlock();
  void drawPattern();
  void drawKeyGrid(const char* title, const char* const* labels, int count, int rows);
  void drawChordSettings();
  mirror::RefreshKind drawFrame();  // draws under mutex_, returns the refresh to run

  static constexpr uint16_t kMaxPayload = 2048;

  mirror::GfxAdapter gfx_;
  mirror::MirrorBle ble_;
  mirror::RefreshPolicy policy_;
  std::unique_ptr<mirror::Session> session_;
  std::mutex mutex_;  // session_/renderer state: written by loop(), read by the render task
  enum class Startup { Ok, NoMemory, Failed } startup_ = Startup::Failed;
  uint32_t seenEpoch_ = 0;
  bool wasConnected_ = false;
  std::string drawnBand_;
  bool forceFull_ = true;
  bool statusShown_ = false;
  bool bondsForgotten_ = false;

  Screen screen_ = Screen::Status;
  Screen drawnScreen_ = Screen::Status;
  bool keyboardOn_ = false;  // opt-in, persisted
  mirror::Chord chord_ = mirror::kDefaultChord;
  mirror::Chord editChord_ = mirror::kDefaultChord;
  int cursor_ = 0;               // selected key (unlock) or row (chord settings)
  std::string note_;             // one line of feedback; never a typed digit
  uint32_t chordSentEpoch_ = 0;  // connection the chord was last announced to the phone on
  bool hidReadyAnnounced_ = false;
  bool phoneWasLocked_ = true;
  uint16_t patternVisited_ = 0;  // dots already sent during this touch; cleared on release, never drawn
};
#endif
