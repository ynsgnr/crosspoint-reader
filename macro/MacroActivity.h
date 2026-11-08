#pragma once
#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
// Macro deck screen: a 3x4 grid of shortcuts sent as a USB HID keyboard (default; boards with USB-OTG) or over the
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

  bool moveCursor();
  void press(int index);
  void sendMacro(int index);
  void startTransport();
  void toggleTransport();
  void leave();
  void reboot();
  bool linkUp() const;     // the host is connected over the active transport
  bool linkReady() const;  // ... and accepts keyboard input
  bool canSwitch() const { return MACRO_HAS_USB != 0; }

  mirror::MirrorBle ble_;
#if MACRO_HAS_USB
  macro::MacroUsb usb_;
#endif
  bool useBle_ = true;  // the active transport
  Start start_ = Start::Failed;
  std::mutex mutex_;  // cursor_ and pressed_: written by loop(), read by the render task
  int cursor_ = 0;    // selected key on boards without touch
  int pressed_ = -1;  // key drawn black while it is touched or sent
  bool connected_ = false;
  bool hidReady_ = false;
  int partials_ = 0;  // partial refreshes since the last full one
  bool fullNext_ = true;
  bool rebooting_ = false;
};
#endif
