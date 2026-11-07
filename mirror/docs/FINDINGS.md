# Findings (measured 2026-11-02 and later, SDK pinned at 5e94fe7)

Short rules from the hardware bugs: `LESSONS.md`. This file keeps the measurements and the detail.

## BLE stack
* The build already has a BLE stack: `[ble_page_turner]` in `platformio.ini` builds NimBLE-Arduino 2.5.1 under the SDK's `BleKeyboardHost` (HID **central** only).
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
* A keyboard appearance (0x03C1) in the advertisement made Android classify the device as a keyboard (class 0x540; AOSP `btm_ble_get_appearance_as_cod`). For a keyboard, `BondStateMachine` PIN_REQUEST invents a random PIN, shows it on the phone and answers the passkey request with it, so the code on our screen is never asked for and pairing ends in "incorrect PIN or passkey" (phone log: `variant=DISPLAY_PIN`, `SMP_PasskeyReply` 90 ms after the request, `SMP_CONFIRM_VALUE_ERR`). The appearance is no longer advertised; the HID service UUID alone only gives class 0x500, which takes the normal passkey dialog.
* Entering Mirror a second time in one boot aborted the firmware when a phone connected: NimBLE kept static callback objects that referenced the first (destroyed) `MirrorBle`, so `std::mutex::lock()` failed in `onConnected`. Found by decoding the panic backtrace with `xtensa-esp32s3-elf-addr2line` against a rebuild of the flashed commit. The callbacks are now re-bound on every `start()` and unbound on `stop()`, and `~MirrorBle` stops the stack.
* A bonded phone that failed to encrypt a reconnect (HCI 0x3D, MIC failure, 30 ms after connecting) lost its bond because the authentication-failure handler deleted it: the device then showed "0 bonds" and the phone had to be removed in Android settings. Only a pairing in progress deletes a bond now. Link-layer procedures (connection update, data length) are no longer started at connect time but after the link is secure, and the device no longer sends its own security request (a late `ble_gap_security_initiate` was followed by `HCI wait for ack` timeouts and a host reset).

## Known gaps from the last review pass (not fixed; behaviour unchanged)
Firmware: NimBLE's repeat-pairing handler deletes the stored bond before our window check (a spoofed identity address can force a re-pair); `send()` can leave a torn frame if a chunk fails after retries (the phone's stream parser realigns on the next resync); `typeKey`/`stop()` still sleep while the activity lock is held (up to about 300 ms, or 3 s for stop); advertising can briefly restart behind `service()` right after a failed connect; the status buttons might overlap the footer on a 480x800 panel (unconfirmed); the dark-mode band says "tap here" on button-only boards; the envelope parser can wait for a stray frame length (bounded by the maximum payload).
Android: sender names differ between notifications and the screen for the same message (short repeats can show twice); times have no day outside WhatsApp (no separators), so a scrolled-up message can sort wrong; a notification from another chat resets the mirrored conversation until the next snapshot; the secure-window probe has no in-flight de-duplication; the node budget is spent depth-first; a bond that Android refuses to remove (hidden API) must be removed in Bluetooth settings.

## Ideas taken from other firmware
* Flowe OS (a separate firmware for the same hardware, github.com/andrewjiang/flowe-os) documents BLE lessons that match ours: wait for every link to close before `NimBLEDevice::deinit` (a heap assert otherwise); callbacks that outlive the stack must not keep a stale owner; GATT objects can leak about 1.7 KB per stop/start cycle (we now log heap at every stop); parse on the main loop, not the host task; 5 s supervision timeout. Not taken: ANCS (iPhone only), Just Works pairing, the sleep poster, SD `update.bin` flashing (would save the cable during testing).

## Not verified (security, keyboard and chord addendum)
Verified on hardware (X4 Pro, Pixel on Android 16): passkey pairing and bonding, reconnect after leaving Mirror, mirroring and scrolling, phone lock state, PIN entry over the HID keyboard. The list below was written before that and is still open for the rest (sniffer check, legacy/Just Works refusal, wake, chord, bonds across a phone reboot). Test in this order:
1. A sniffer (nRF Sniffer) after pairing shows no plaintext; an unbonded central cannot read, write or subscribe, including the HID characteristics and the CCCDs.
   Risk: `ble_gatts_set_clt_cfg_perm_flags` is a private NimBLE function (declared by hand in `MirrorBle.cpp`); confirm the CCCD really asks for encryption.
2. Only LE Secure Connections with passkey is accepted: try a legacy-only and a Just Works central. `sm_sc_only` also refuses phones without Secure Connections.
3. One pairing gives both the HID keyboard and the app's GATT link; forgetting on either side removes both; re-pairing from clean works. Android keeps the HID connection itself;
   neither side has a keyboard switch any more (protocol 4).
4. The keyboard types the PIN on the lock screen, also after a reboot before first unlock (the app cannot run then; the pad types blind). Android may require the screen to be on: test the wake sent when the unlock screen opens.
5. Which wake mechanism works (Shift tap vs System Wake Up usage) per phone. Android's on-screen keyboard is normally hidden while a hardware keyboard is connected; check yours.
6. The system HID connection and the app's GATT connection on one link; connection parameters (`updateConnParams` 15-30 ms) with HID traffic; the NimBLE host's connection count.
7. The accessibility service receives the chord with the screen on, off and on the lock screen; the event is consumed while every other key (PIN digits) passes through; Android lets the service start the foreground service from the background
   (`REQUEST_COMPANION_START_FOREGROUND_SERVICES_FROM_BACKGROUND` is declared, but needs a Companion association, which `CompanionPairing` makes).
8. Pattern locks were tried (digits 0-8 as dots, dragged) and removed: the gesture did not work. Only PIN is supported. The pad sends main-row digits only; a lock screen that wants keypad digits needs `0x59-0x62` (not implemented).
9. X4 Pro: MCU (C3 vs S3), heap headroom with HID + mirror service (the C3 image grew: see the Flash and RAM table above), touch tap coordinates in `wasScreenTapped`, and that the page-turner ring is off while Mirror is open.
10. Bonds survive a reboot on both sides; nothing about a PIN is persisted (only `sec_epoch` is in NVS; older builds also left `kbd`, `chord_mod`, `chord_key`, now unread).
11. The launcher opens each app from the background (the phone's accessibility service context), also while dark mode is on; scrolling past the oldest message drags the chat app's list without a fling and the next reading overlaps the last one, in WhatsApp, Signal, Discord and the Claude app.

## Not verified
* Not measured on hardware: throughput, real refresh quality and ghosting cadence, heap headroom over many visits (the heap is now logged at every stop), battery. No emulator was run (no KVM).
* Running the C3 image: it compiles and links; whether the `-U` role flags and BLE peripheral work on the C3 controller is untested.
* The `CoverGridHomeUi` change was compiled, not seen on a device. Mirror's cover-grid icon reuses the Transfer icon.
* Chat app selectors: which apps were checked against a real capture is listed in the retink README ("Limits"). Telegram needs a content-description parser.
* `FLAG_SECURE` probe behaviour on real devices (needs API 34+; rate limited).
* Dark mode is brightness-only now (the overlay, timeout and proximity guard were removed after the overlay locked the phone up on hardware). The capture export was compiled and unit-tested only.
* Repo process: `firmware-handoff` (four independent reviews, human acceptance, hardware testing) has not been done.
