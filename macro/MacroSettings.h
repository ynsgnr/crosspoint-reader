#pragma once
#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
// The Macro deck's persisted setting (NVS namespace "macro").
namespace macro::settings {

// True when the deck sends over Bluetooth (the Mirror BLE keyboard) instead of USB. Off unless set: USB is the default.
// Builds without USB device support (the X4's ESP32-C3) ignore it and always use Bluetooth.
bool bleEnabled();
void setBleEnabled(bool enabled);

}  // namespace macro::settings
#endif
