# macro/: macro deck for the Xteink X4 / X4 Pro

A **Macro** entry on Home opens a 3x3 grid of shortcuts. A tap (or Confirm on the cursor) sends one shortcut to the paired
host over the BLE HID keyboard of the `mirror/` module: media keys (consumer report) or a key with modifiers. Nothing is stored.

```
Macro.h              the shortcut table (portable, host tested)
MacroActivity.*      the screen (firmware only)
build.py             adds MacroActivity.cpp to the build
platformio_macro.ini environments `macro` (X4) and `x4pro-macro` (X4 Pro)
tests/               host test of the table
```

## Use
1. In **Mirror**: switch **Keyboard** on, tap **Pair phone**, and pair the host (a phone or a computer; the passkey shows on the device).
2. Open **Macro** from Home. With a bonded host in range it connects by itself; the grid shows once it is connected.
3. Without a bond, the screen says what to do in Mirror. Pairing is never done here.

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
* **Nothing here has been compiled for the device or run on hardware.** Only the host test of the table ran.
* `macro/build.py` appends to `SRC_FILTER` after `mirror/build.py` replaced it; the order of `extra_scripts` and the append on a
  string variable are untested.
* Media keys and Ctrl+C/V/Z reach a phone and a PC over the same bond; a press and release 15 ms apart is not dropped by the host.
* Bonded host reconnects while Macro is open (it advertises the Mirror service UUID and name, same as Mirror).
* The cover-grid Home (X4 Pro default) with 8 tabs: fit and icon. Macro reuses the Transfer icon, like Mirror.
* Heap with the BLE stack up on the C3 and the S3 (the stack is stopped on exit).
