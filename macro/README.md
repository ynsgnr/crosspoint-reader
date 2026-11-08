# macro/: macro deck for the Xteink X4 / X4 Pro

A **Macro** entry on Home opens a 3x3 grid of shortcuts. A tap (or Confirm on the cursor) sends one shortcut to the host as a
**USB HID keyboard** (default): media keys (consumer report) or a key with modifiers. A **Bluetooth** setting, the switch
below the grid, sends over the BLE HID keyboard of the `mirror/` module instead. Nothing else is stored.

```
Macro.h              the shortcut table (portable, host tested)
MacroActivity.*      the screen (firmware only)
MacroUsb.*           USB HID keyboard + media keys (Arduino USB/TinyUSB; boards with USB-OTG only)
MacroSettings.*      the Bluetooth on/off setting (NVS namespace "macro", default off = USB)
build.py             adds MacroActivity.cpp to the build
platformio_macro.ini environments `macro` (X4) and `x4pro-macro` (X4 Pro)
tests/               host test of the table
```

## Use
**USB (default, X4 Pro):** open **Macro** and plug the USB cable into the host; the grid shows once the host has enumerated
the device. The X4 Pro appears as a keyboard while Macro is open. TinyUSB cannot be stopped, so closing Macro reboots to Home.

**Bluetooth:** tap the switch under the grid (or select it and press Confirm) to turn Bluetooth on; the choice is saved.
1. In **Mirror**, tap **Pair phone** and pair the host (the passkey shows on the device).
2. Open **Macro**. With a bonded host in range it connects by itself; pairing is never done here.

The X4 (ESP32-C3) has no USB device mode, so it always uses Bluetooth and shows no switch.

Edit the table in `Macro.h` to change the keys, then rebuild. Consumer usages are from the HID Usage Tables, page 0x0C.

## Build and test
```bash
pio run -e x4pro-macro -t upload    # X4 Pro (ESP32-S3)
pio run -e macro -t upload          # X4 (ESP32-C3)
cmake -S macro -B build-macro && cmake --build build-macro && ctest --test-dir build-macro --output-on-failure
```
Without `-DCROSSPOINT_MACRO=1` (every other environment) none of this is compiled. Macro needs the mirror module, so the macro
environments extend the mirror ones.

## Not verified
* **Nothing here has been compiled for the device or run on hardware.** Only the host test of the table ran. The USB path (`MacroUsb.cpp`) is the newest and least checked: `USBHIDKeyboard` + `USBHIDConsumerControl` with `USB.begin()` next to hardware CDC/JTAG, and the reboot on leaving.
* The switch row below the grid fits above the footer lines on the X4 Pro screen (not seen on a display).
* `macro/build.py` appends to `SRC_FILTER` after `mirror/build.py` replaced it; the order of `extra_scripts` and the append on a
  string variable are untested.
* Media keys and Ctrl+C/V/Z reach a phone and a PC over the same bond; a press and release 15 ms apart is not dropped by the host.
* Bonded host reconnects while Macro is open (it advertises the Mirror service UUID and name, same as Mirror).
* The cover-grid Home (X4 Pro default) with 8 tabs: fit and icon. Macro reuses the Transfer icon, like Mirror.
* Heap with the BLE stack up on the C3 and the S3 (the stack is stopped on exit).
