#pragma once
#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
// Macro deck screen: a 3x3 grid of shortcuts sent as a USB HID keyboard (default; boards with USB-OTG) or over the
// Mirror module's BLE HID keyboard (setting, and the only choice without USB-OTG). BLE only reuses the bond made in
// Mirror; pairing stays there. See macro/README.md.
#include <mutex>

#include "Macro.h"
#include "MacroUsb.h"
#include "activities/Activity.h"
#include "device/MirrorBle.h"

class MacroActivity final : public Activity {
 public:
  MacroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Macro", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return linkUp(); }

 private:
  enum class Start { Ok, NoMemory, Failed };

  bool moveCursor(bool gridShown);
  void sendMacro(int index);
  void startTransport();
  void toggleTransport();
  void leave();
  bool linkUp() const;     // the host is connected over the active transport
  bool linkReady() const;  // ... and accepts keyboard input
  bool canSwitch() const { return MACRO_HAS_USB != 0; }
  int toggleIndex() const { return macro::kMacroCount; }  // the switch below the grid shares the cursor

  mirror::MirrorBle ble_;
#if MACRO_HAS_USB
  macro::MacroUsb usb_;
#endif
  bool useBle_ = true;  // the active transport
  Start start_ = Start::Failed;
  std::mutex mutex_;    // cursor_ and note_: written by loop(), read by the render task
  int cursor_ = 0;      // selected key (or the switch) on boards without touch
  char note_[48] = "";  // one line of feedback
  bool connected_ = false;
  bool hidReady_ = false;
  int partials_ = 0;  // partial refreshes since the last full one
  bool fullNext_ = true;
};
#endif
