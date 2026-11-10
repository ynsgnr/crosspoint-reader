#pragma once

// Releases the ESP32-S3 USB-OTG PHY and reconnects the hardware USB
// Serial/JTAG peripheral before a normal application restart. No-op on targets
// that do not enable the USB Drive capability (FREEINK_CAP_USB_MSC).
void handoffUsbOtgToSerialJtag();

// First thing in setup(): if the USB-OTG PHY was left selected (a crash while an OTG device such as the Macro keyboard
// was up), give it back to Serial/JTAG and restart once. Does nothing when the PHY is already Serial/JTAG, and tries
// at most once until the PHY has been seen clear, so it cannot loop.
void restoreSerialJtagIfOtgLeftOn();
