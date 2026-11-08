#pragma once
#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
// Macro deck screen: a 3x3 grid of shortcuts sent over the Mirror module's BLE HID keyboard. It only reuses the
// bond made in Mirror (Keyboard on); pairing stays there. See macro/README.md.
#include <mutex>

#include "Macro.h"
#include "activities/Activity.h"
#include "device/MirrorBle.h"

class MacroActivity final : public Activity {
 public:
  MacroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Macro", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return ble_.connected(); }

 private:
  enum class Start { KeyboardOff, Ok, NoMemory, Failed };

  bool moveCursor();
  void sendMacro(int index);

  mirror::MirrorBle ble_;
  Start start_ = Start::KeyboardOff;
  std::mutex mutex_;    // cursor_ and note_: written by loop(), read by the render task
  int cursor_ = 0;      // selected key on boards without touch
  char note_[48] = "";  // one line of feedback
  bool connected_ = false;
  bool hidReady_ = false;
  int partials_ = 0;  // partial refreshes since the last full one
  bool fullNext_ = true;
};
#endif
