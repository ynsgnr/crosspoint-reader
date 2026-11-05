#pragma once
#if defined(CROSSPOINT_MIRROR)
// Mirror screen: BLE advertising and the connection exist only while it is open.
// MirrorActivity.cpp: lifecycle, input loop, status and chat drawing. MirrorScreens.cpp: unlock pad, chord
// settings.
#include <memory>
#include <mutex>
#include <string>

#include "activities/Activity.h"
#include "core/Chord.h"
#include "core/RefreshPolicy.h"
#include "core/Session.h"
#include "device/ButtonColumn.h"
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
  // Stay awake (full CPU speed, no auto-sleep) while a phone is connected or the pairing window is open.
  bool preventAutoSleep() override { return ble_.connected() || ble_.pairingOpen(); }

 private:
  enum class Screen { Status, Unlock, ChordSettings };
  enum class Action { None, PairPhone, ForgetPhone, ToggleKeyboard, StartMirror, Unlock, ChordSettings };

  // State of the current link, cleared whenever the connection changes.
  struct Link {
    bool connected = false;
    uint32_t epoch = 0;
    bool chordAnnounced = false;
    bool hidAnnounced = false;
    bool phoneWasLocked = true;
  };

  // MirrorActivity.cpp
  void resetSession();
  void startBle();
  void setScreen(Screen s);
  void onLinkChanged();
  void announceKeyboardToPhone();
  bool phoneAllowsKeyboard() const;
  bool loopStatus();
  bool loopChat();
  void buildActions();
  void runAction(Action action);
  void drawStatus();
  mirror::RefreshKind drawFrame();  // draws under mutex_, returns the refresh to run

  // MirrorScreens.cpp; the loops return true when the screen needs a redraw and run with mutex_ held.
  bool loopUnlock();
  bool loopChordSettings();
  void buildChordButtons();
  int chordButtonsTop() const;
  void openUnlock();
  void sendDueEnter();          // the Enter that follows the wake, once the screen has had time to light up
  bool sendKey(uint8_t usage);  // false when the keyboard is not ready
  void sendChordToPhone();
  bool moveCursor(int count);
  bool leaveWhenUnlocked();
  void drawKeyGrid(const char* title, const char* const* labels, int count, int rows);
  void drawChordSettings();
  void drawSubScreen();

  static constexpr uint16_t kMaxPayload = 2048;

  mirror::GfxAdapter gfx_;
  mirror::MirrorBle ble_;
  mirror::RefreshPolicy policy_;
  std::unique_ptr<mirror::Session> session_;
  std::mutex mutex_;  // session_ and screen state: written by loop(), read by the render task
  mirror::MirrorBle::StartResult startup_ = mirror::MirrorBle::StartResult::Failed;
  Link link_;

  Screen screen_ = Screen::Status;
  Screen drawnScreen_ = Screen::Status;
  bool keyboardOn_ = false;  // opt-in, persisted
  mirror::Chord chord_ = mirror::kDefaultChord;
  mirror::Chord editChord_ = mirror::kDefaultChord;
  int cursor_ = 0;    // selected key (unlock screens) or row (chord settings)
  std::string note_;  // one line of feedback; never a typed digit
  bool bondsForgotten_ = false;
  uint32_t enterDueMs_ = 0;  // when to send Enter after waking the phone (0 = none pending)
  mirror::ButtonColumn buttons_;
  Action armed_ = Action::None;  // a destructive action waiting for its second tap

  // Drawing state.
  std::string drawnBand_;
  bool forceFull_ = true;
  bool statusShown_ = false;
  int subScreenPartials_ = 0;  // partial refreshes since the last full one on the non-chat screens
};
#endif
