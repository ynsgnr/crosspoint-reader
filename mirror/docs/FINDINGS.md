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
| `x4pro-mirror` | 5,458,698 (+30,552 B, 83.3%) | 111,772 (+32 B) |
| `default` (X4, ESP32-C3) stock | 5,584,045 (85.2%) | 66,680 |
| `mirror` (X4, ESP32-C3) | 5,629,217 (+45,172 B, 85.9%) | 66,960 (+280 B) |

* Headroom: about 1.1 MB flash on the X4 Pro, about 0.9 MB on the X4. Runtime heap use of the NimBLE host + GATT server was **not measured**; the S3 profile
  places the NimBLE host in PSRAM (`scripts/s3_nimble_psram.h`). `MirrorBle` refuses to start below a 24 KB largest free block
  (unmeasured estimate).
* X4 (ESP32-C3) builds with pioarduino core 6.1.19 (PlatformIO 6.2.0 from PyPI fails in the core rebuild on `SCons.Tool.FortranCommon`, also for the
  unmodified `env:default`; upstream CI pins 6.1.19 and the mirror workflow copies that). Static RAM only: C3 heap with the BLE server up is unmeasured.

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

## Found on hardware
* Advertising with `BLE_OWN_ADDR_RPA_RANDOM_DEFAULT` fails on the X4 Pro: `Error enabling advertising; rc=530` (HCI 0x12, invalid parameters). The public address works, so the mirror uses it.
* The device name did not fit in the main advertising packet (`Data length exceeded`); it now goes in the scan response.

## Not verified (security, keyboard and chord addendum)
Everything below is implemented and compiles (both firmware images, host tests, Kotlin tests, APK) but has NOT been run on hardware. Test in this order:
1. A sniffer (nRF Sniffer) after pairing shows no plaintext; an unbonded central cannot read, write or subscribe, including the HID characteristics and the CCCDs.
   Risk: `ble_gatts_set_clt_cfg_perm_flags` is a private NimBLE function (declared by hand in `MirrorBle.cpp`); confirm the CCCD really asks for encryption.
2. Only LE Secure Connections with passkey is accepted: try a legacy-only and a Just Works central. `sm_sc_only` also refuses phones without Secure Connections.
3. One pairing gives both the HID keyboard and the app's GATT link; forgetting on either side removes both; re-pairing from clean works. Android keeps the HID connection itself;
   the app's "keyboard" switch only changes what the app and the device do (`keyboard_allowed`), not the OS input-device profile.
4. The keyboard types the PIN on the lock screen, also after a reboot before first unlock (the app cannot run then; the pad types blind). Android may require the screen to be on: test the wake sent when the unlock screen opens.
5. Which wake mechanism works (Shift tap vs System Wake Up usage) per phone. Android's on-screen keyboard is normally hidden while a hardware keyboard is connected; check yours.
6. The system HID connection and the app's GATT connection on one link; connection parameters (`updateConnParams` 15-30 ms) with HID traffic; the NimBLE host's connection count.
7. The accessibility service receives the chord with the screen on, off and on the lock screen; the event is consumed while every other key (PIN digits) passes through; Android lets the service start the foreground service from the background
   (`REQUEST_COMPANION_START_FOREGROUND_SERVICES_FROM_BACKGROUND` is declared, but needs a Companion association, which `CompanionPairing` makes).
8. Pattern lock: the user found in the Android source that the digits 0-8 (row by row) drive the pattern view ("01258" for 0>1>2>5>8). Unverified here: whether Enter after the last dot is needed or harmful, and whether a fast finger skips dots. The pad sends main-row digits only; a lock screen that wants keypad digits needs `0x59-0x62` (not implemented).
9. X4 Pro: MCU (C3 vs S3), heap headroom with HID + mirror service (the C3 image grew: see `build size` in the PR), touch tap coordinates in `wasScreenTapped`, and that the page-turner ring is off while Mirror is open.
10. Bonds survive a reboot on both sides; nothing about a PIN is persisted (only `kbd`, `chord_mod`, `chord_key`, `sec_epoch` are in NVS).

## Not verified
* Anything on hardware or a phone: pairing UX, advertising, connection parameters, throughput, real refresh quality and ghosting
  cadence, heap headroom, battery. No emulator was run (no KVM); the app was compiled (`assembleDebug`) and its logic unit-tested on the JVM.
* Running the C3 image: it compiles and links; whether the `-U` role flags and BLE peripheral work on the C3 controller is untested.
* The `CoverGridHomeUi` change was compiled, not seen on a device. Mirror's cover-grid icon reuses the Transfer icon.
* App resource ids in `ChatAppSelectors` (from memory, unvalidated); Telegram needs a content-description parser.
* `FLAG_SECURE` probe behaviour on real devices (needs API 34+; rate limited).
* Dark mode (overlay, `getWindows()`, proximity guard) and the capture export were compiled and unit-tested only. Open questions: whether `BRIGHTNESS_OVERRIDE_OFF` dims
  on the target phone, whether `getWindows()` returns the chat window while `packageNames` restricts events, and what `FLAG_SECURE` probing returns on API < 34.
* Repo process: `firmware-handoff` (four independent reviews, human acceptance, hardware testing) has not been done.
