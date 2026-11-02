# Findings (measured 2026-11-02, branch `claude/elegant-shannon-i9uuis`, SDK pinned at 5e94fe7)

## BLE stack
* Yes. `[ble_page_turner]` in `platformio.ini` builds NimBLE-Arduino 2.5.1 under the SDK's `BleKeyboardHost` (HID **central** only).
  The peripheral and broadcaster roles are compiled out with `-DCONFIG_BT_NIMBLE_ROLE_{PERIPHERAL,BROADCASTER}_DISABLED=1`
  (NimBLE-Arduino `nimconfig.h` gates each role on those macros). The X4 Pro (`env:x4pro`) uses the prebuilt Arduino core
  with Bluetooth; the X4 (`env:default`) rebuilds the core with `custom_sdkconfig`.
* `mirror/platformio_mirror.ini` lifts the two macros with `-U` (verified: `NimBLEServer::start` and the mirror code are in
  the `x4pro-mirror` image). MTU is requested at runtime (`NimBLEDevice::setMTU(247)`): the C3 core bakes MTU 23 into `sdkconfig.h`.
* `lib/BlePageTurner` owns the radio for HID remotes; it stays off while Mirror is open (see `MIRROR_CHANGES.md`).

## Flash and RAM (PlatformIO 6.2.0, `pio run -e x4pro*`)
| build | flash (of 6,553,600 app partition) | static RAM (of 327,680) |
| --- | --- | --- |
| `x4pro` upstream baseline | 5,428,146 (82.8%) | 111,740 |
| `x4pro` with the hooks, flag off | 5,428,242 (+96 B) | 111,740 |
| `x4pro-mirror` | 5,455,514 (+27,368 B, 83.2%) | 111,772 (+32 B) |

* Headroom on the X4 Pro: about 1.1 MB flash. Runtime heap use of the NimBLE host + GATT server was **not measured**; the S3 profile
  places the NimBLE host in PSRAM (`scripts/s3_nimble_psram.h`). `MirrorBle` refuses to start below a 24 KB largest free block
  (unmeasured estimate).
* The X4 (ESP32-C3) build could not be run in the sandbox: PlatformIO 6.2.0 fails in the core rebuild
  (`No module named 'SCons.Tool.FortranCommon'`), also on the unmodified `env:default`. C3 flash/RAM and heap are unknown.

## Display
* `HalDisplay` modes: `FULL_REFRESH`, `HALF_REFRESH`, `FAST_REFRESH`. FAST is the SDK's differential waveform against the
  controller's retained baseline: only changed pixels are driven.
* The SDK has `displayWindow(x, y, w, h)` (marked EXPERIMENTAL; SSD1677 needs byte-aligned x and w and allocates two vectors per call), but
  `HalDisplay` does not expose it and `GfxRenderer::displayWindow` is commented out. Using it needs a one-method HAL addition (an upstream edit),
  so the mirror uses full-frame FAST and applies its own policy (HALF every 20 partials, per-template override).

## Fonts
`GfxRenderer::getTextWidth / getLineHeight / truncatedText / wrappedText`, font ids `UI_10`, `UI_12`, `SMALL` in `src/fontIds.h`
(UI families have a bold face). `GfxAdapter` maps them to the portable `Gfx` interface; the capabilities handshake reports three fonts.

## Menu and activities
`HomeActivity` builds its menu from index arithmetic (`getMenuItemCount`, `activateSelection`, `render`), and the cover-grid theme
(`CoverGridHomeUi`, the PSRAM-board default) draws a separate fixed tab list. A Home entry therefore touches both. Activities need no
registration: `activityManager.replaceActivity(...)` starts one. Replace (not push) frees Home's buffers before the BLE stack starts.

## Not verified
* Anything on hardware or a phone: pairing UX, advertising, connection parameters, throughput, real refresh quality and ghosting
  cadence, heap headroom, battery. No emulator was run (no KVM); the app was compiled (`assembleDebug`) and its logic unit-tested on the JVM.
* The C3 build (above). `-U` role flags on the C3 core rebuild path.
* The `CoverGridHomeUi` change was compiled, not seen on a device. Mirror's cover-grid icon reuses the Transfer icon.
* App resource ids in `ChatAppSelectors` (from memory, unvalidated); Telegram needs a content-description parser.
* `FLAG_SECURE` probe behaviour on real devices (needs API 34+; rate limited).
* Repo process: `firmware-handoff` (four independent reviews, human acceptance, hardware testing) has not been done.
