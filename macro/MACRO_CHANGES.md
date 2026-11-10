# MACRO_CHANGES: files outside `macro/` touched by the macro deck

Everything else is new files under `macro/`. Without `-DCROSSPOINT_MACRO=1` (set only by the `macro` and `x4pro-macro`
environments) the firmware is the same as the mirror build: the hooks are `MACRO_ENABLED` guarded (0 without the flag) and
no macro header is included. The flag also requires `CROSSPOINT_MIRROR`, because the deck uses the mirror BLE stack.

| File | Change | Why |
| --- | --- | --- |
| `platformio.ini` | one more line in `extra_configs`: `macro/platformio_macro.ini` | defines the `macro` and `x4pro-macro` environments |
| `src/activities/home/HomeActivity.cpp`, `.h` | a `MACRO_ENABLED` block next to the `MIRROR_ENABLED` one; `MirrorHomeItem::Macro`; the item counts, the More... selection, the activation and the list entry "Macro" follow Mirror; `onMacroOpen()` | Home menu entry (list Home and cover-grid Home) |
| `src/components/CoverGridHomeUi.cpp`, `.h` | the same pattern: tab loop 7 -> 8 slots, one icon-table entry, one `present` entry, `tabItems` size 8 | Home entry for the cover-grid theme |
| `src/activities/home/HomeActivity.cpp` (auto-open) | a `MACRO_ENABLED` block at the top of `loop()` that calls `macro::autoopen::shouldOpen()` | Macro opens by itself when a PC is on the USB cable (setting, off by default) |
| `src/platform/UsbSerialJtagHandoff.cpp`, `.h`; `src/main.cpp` | `restoreSerialJtagIfOtgLeftOn()` and one call at the start of `setup()` (not macro-guarded: no-op unless the OTG PHY was left selected) | after a crash with the Macro keyboard up the serial port comes back by itself |
| `.github/workflows/mirror.yml` | the build matrix uses `x4pro-macro` and `macro` (they extend `x4pro-mirror` and `mirror` plus `-DCROSSPOINT_MACRO=1`); the artifact names are unchanged | CI compiles the macro code too |
| `mirror/device/MirrorHid.h`, `.cpp`, `MirrorBle.h` | `MirrorHid::tapConsumer` and `MirrorBle::typeConsumer` (about 17 lines); the consumer characteristic is kept in `consumerIn_` | media keys: the characteristic was private to `MirrorHid` and created without a handle. Mirror behaviour is unchanged. |

Not touched: `mirror/` docs and tests, `ActivityManager`, translations (the label "Macro" is a plain string, like "Mirror"),
`partitions*.csv`, the SDK submodule, the other CI workflows.

To check the list after a rebase: `git diff --stat <base> -- . ':!macro'` shows only the files above.
