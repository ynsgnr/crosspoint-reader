#pragma once
#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
// USB HID keyboard + media keys for the Macro deck (Arduino USB/TinyUSB). Only boards with USB-OTG
// (FREEINK_CAP_USB_MSC, the X4 Pro) have it; on the others MACRO_HAS_USB is 0 and the deck uses Bluetooth only.
#include <cstdint>

#if FREEINK_CAP_USB_MSC
#define MACRO_HAS_USB 1
#else
#define MACRO_HAS_USB 0
#endif

#if MACRO_HAS_USB
namespace macro {

class MacroUsb {
 public:
  // Registers the HID interfaces and starts TinyUSB. TinyUSB cannot be stopped again: leave the screen with
  // restartToHomeAfterStorageHandoff(). False when the heap or the USB stack refused.
  bool start();
  bool started() const { return started_; }
  bool connected() const;  // the host has enumerated the device
  bool ready() const;      // the host accepts HID input reports
  bool tapKey(uint8_t mods, uint8_t usage);
  bool tapConsumer(uint16_t usage);

 private:
  bool started_ = false;
};

}  // namespace macro
#endif
#endif
