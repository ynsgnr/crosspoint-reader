#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
#include "MacroUsb.h"

#if MACRO_HAS_USB
#include <Arduino.h>
#include <USB.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>

#include <new>

extern "C" bool tud_mounted(void);
extern "C" bool tud_disconnect(void);
extern "C" bool tud_hid_n_ready(uint8_t instance);

namespace macro {
namespace {
// Created on first start and never freed: the HID interfaces cannot be unregistered, the screen reboots to leave.
USBHIDKeyboard* gKeyboard = nullptr;
USBHIDConsumerControl* gConsumer = nullptr;
constexpr uint32_t kReleaseDelayMs = 15;  // hosts drop a press and release that arrive back to back
}  // namespace

bool MacroUsb::start() {
  if (started_) return true;
  // The keyboard goes first: the first USBHID object fixes the interface's boot-protocol setting.
  if (!gKeyboard) gKeyboard = new (std::nothrow) USBHIDKeyboard();
  if (!gConsumer) gConsumer = new (std::nothrow) USBHIDConsumerControl();
  if (!gKeyboard || !gConsumer) return false;
  gKeyboard->begin();
  gConsumer->begin();
  started_ = USB.begin();
  return started_;
}

void MacroUsb::detach() {
  if (!started_) return;
  tud_disconnect();  // drops the D+ pull-up, so the host sees the keyboard unplugged
  for (int i = 0; i < 50 && tud_mounted(); ++i) delay(10);
}

bool MacroUsb::connected() const { return started_ && tud_mounted(); }

bool MacroUsb::ready() const { return connected() && tud_hid_n_ready(0); }

bool MacroUsb::tapKey(uint8_t mods, uint8_t usage) {
  if (!ready()) return false;
  KeyReport down = {};
  down.modifiers = mods;
  down.keys[0] = usage;
  gKeyboard->sendReport(&down);
  delay(kReleaseDelayMs);
  KeyReport up = {};  // always release
  gKeyboard->sendReport(&up);
  delay(kReleaseDelayMs);
  return true;
}

bool MacroUsb::tapConsumer(uint16_t usage) {
  if (!ready()) return false;
  const bool pressed = gConsumer->press(usage) != 0;
  delay(kReleaseDelayMs);
  gConsumer->release();  // always release
  delay(kReleaseDelayMs);
  return pressed;
}

}  // namespace macro
#endif
#endif
