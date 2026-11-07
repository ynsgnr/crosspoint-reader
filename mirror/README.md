# mirror/: BLE text mirror for the Xteink X4 / X4 Pro

Device side of `ynsgnr/retink`. The Mirror screen (last Home menu item) advertises as `E-Ink Reader` while it is open and
renders the template the phone sends. Security, pairing, the keyboard, the app launcher and the trigger chord are described below. Upstream touchpoints: `MIRROR_CHANGES.md`. Findings and unverified items: `docs/FINDINGS.md`. Lessons from the hardware bugs: `docs/LESSONS.md`.

```
core/        Wire, Envelope, Session (ack/resync/errors), TemplateRenderer + registry, RefreshPolicy, Gfx   (portable)
templates/   chat/: ChatRenderer + ChatLayout                                                               (portable)
device/      MirrorActivity (+ MirrorScreens: unlock pad), MirrorBle, MirrorHid, MirrorSettings, GfxAdapter (firmware only)
generated/   written by retink/scripts/sync-fork.sh; MANIFEST.sha256 detects hand edits
sim/ tests/  host simulator (PNG frames) and tests; test/golden/ = transcripts from the Kotlin tests
```

## Build
```bash
pio run -e x4pro-mirror -t upload   # X4 Pro (ESP32-S3)
pio run -e mirror -t upload         # X4 (ESP32-C3)
```
Without `-DCROSSPOINT_MIRROR=1` (every other environment) none of this is compiled. Use the PlatformIO core CI uses (the pioarduino 6.1.19 zip, see `.github/workflows/mirror.yml`): PlatformIO 6.2.0 from PyPI fails in the X4 core rebuild. Pull requests build the X4 Pro image only (`firmware-x4pro-mirror.bin`, artifact of the `mirror` workflow); the X4 image builds on manual dispatch.

First pairing: open Mirror and tap Pair phone, then in the Retink app tap "2. Pair e-ink device" and type the 6-digit code shown here. After that the phone reconnects by itself whenever Mirror is open.

`generated/` and `test/golden/` come from the Android repo (`ynsgnr/retink`): run its `scripts/sync-fork.sh` with this repo at `../crosspoint-reader`; never edit them by hand.

## Host tests and simulator
```bash
cmake -S mirror -B build-mirror && cmake --build build-mirror -j && ctest --test-dir build-mirror --output-on-failure
build-mirror/mirror_sim mirror/test/golden/chat/typing.txt out/   # frame_NNN.png (dirty rects in red) + frames.txt
mirror/scripts/check-manifest.sh                                    # generated files untouched
```
The tests replay the Kotlin-recorded transcripts (full round trip, diffs, dropped message, CRC corruption, schema
mismatch, scroll events) byte for byte through the real `Session` and `ChatRenderer`.

## Chat screen
Title row (scroll marker `vN` when newer messages are below, unread badge), messages (incoming left with a thin bar, own right-aligned
as one block with a thick bar), and the draft box with its cursor. The box is 3 lines tall and grows with the text (up to most of the
screen, leaving room for one message); when the draft is longer it shows the end, or the cursor's line when the cursor is further up.
Up/Down (or the page buttons) scroll one message, holding keeps scrolling (see Scrolling); Back opens the menu.

## Security (protocol 3)
* **Encrypted and authenticated only.** Every characteristic (mirror `data`, `event`, `caps`, and every HID/battery/device-info characteristic) has `*_ENC | *_AUTHEN`;
  the notification subscription (CCCD) is made `READ/WRITE_ENC|AUTHEN` for the whole GATT server with `ble_gatts_set_clt_cfg_perm_flags` (`MirrorBle.cpp`). A second check in `onWrite`
  drops data from a link that is not encrypted and authenticated. No debug characteristic exists; nothing logs message text.
* **LE Secure Connections only** (`ble_hs_cfg.sm_sc_only`), MITM on, display-only IO: the passkey is shown on the e-ink screen and typed on the phone. Just Works cannot produce an authenticated bond,
  so such a link is disconnected (`onAuthComplete`).
* **Pairing window.** An unknown phone may connect only for 2 minutes after you tap **Pair phone** on the Mirror screen. A bonded phone always may, but cannot re-pair outside the window
  (`onPasskeyDisplay` disconnects). Pairing a new phone deletes the old bond (one phone at a time). Nothing is advertised without a bond or an open window.
* **Privacy.** The public address is used: advertising with the host-based private address failed on the X4 Pro (HCI error 0x12, serial log), so the device can be recognised by its address. It advertises only while the pairing window is open or a bond exists. The advertisement holds the mirror service UUID plus the HID UUID; the generic name `E-Ink Reader` is in the scan response.
  No content, serial or per-device data. (The mirror service UUID is a project constant; the Companion Device Manager filters on it.)
* **Migration.** `sec_epoch` in NVS (`kSecurityEpoch`, now 4: the keyboard became always on, which changes the GATT database); bonds made under an older model are deleted at the next start. The app does the same (`DeviceBond.SECURITY_EPOCH`).

## Controls
The Mirror screen has a column of buttons (`ButtonColumn`; the launcher's apps sit two to a row): tap them on touch boards (the X4 Pro has no Confirm button by default); on boards without touch, move the cursor with the arrow keys and press Confirm.
* Not linked: **Pair phone**, **Forget phone** (asks for a second tap: it discards the pairing).
* Linked: **Start mirror on phone** until the phone's app answers, **Unlock phone** only while the phone reports it is locked, and one button per app the phone sent (the launcher).
* In the chat view the side buttons or a swipe scroll (holding a button keeps scrolling), and tapping the top of the screen (or Confirm) toggles the phone's dark mode. **Back** goes up one level: chat -> menu (the same page with **Back to chat**, the launcher and unlock), menu -> leave Mirror; the unlock pad -> back.

## Scrolling
Up/Down (or the page buttons, or a swipe) move the view at once through the messages the device holds and send a scroll event; holding a button repeats every 350 ms. The phone keeps the whole conversation and sends the device a window around its view that fits its max payload (`ChatCodec.fitTo`), so the view moves on past what the device holds after one round trip. Past the oldest message it holds, the phone scrolls the chat app itself, so older messages appear without touching the phone; returning to the newest scrolls the app back down.

## Launcher
The phone sends the supported chat apps it has installed (`PhoneStatus.apps`, labels only). Tapping one sends `launch_app` with its index; the phone opens the app and the view returns to the chat, which follows the app that opens.

## Keyboard, unlock and trigger chord
* Always on: the same bond and the same pairing carry the mirror and the HID keyboard; there is no second pairing entry and no switch.
* `MirrorHid`: HID-over-GATT keyboard + consumer + system-control report map, HID Information, Protocol Mode, Control Point, Battery, PnP. Built by hand (not `NimBLEHIDDevice`) so all of it needs the authenticated link.
* **Unlock pad** (button **Unlock phone**, shown while the phone is locked): opening it wakes the phone (a Shift tap, then the System Wake Up usage) and sends Enter so the PIN prompt is up. The PIN pad (1-9, 0, Enter) works by touch or with the arrow keys + Confirm. Pattern locks are not supported.
  PIN keys are sent and released at once and never stored. The phone reports `locked`; the pad closes by itself when it unlocks.
* **Trigger chord**: **Start mirror on phone** (before the phone app runs) sends Ctrl+Alt+Shift+M over HID (`core/Chord.h`, fixed; the phone matches the same chord).

## Status band and dark mode
Every template frame has a one-line band on top (`core/Frame.cpp`, `BandedGfx`): a dot on the left, hollow while the phone's dark mode is off and filled while it is on, and `Back: menu` on the right. Tapping the top of the screen (or Confirm) sends the `toggle_dark_mode` command to the phone; the new state comes back as a `status` message.

## Refresh policy
`FAST_REFRESH` (differential, changed pixels only) for partial updates; `HALF_REFRESH` for the first frame, template
switch, resync, and every 20 partials (`RefreshOverrides` per template). Dirty rectangles are computed per paint item; typing
dirties one 24 px line. `HalDisplay` does not expose the SDK's experimental `displayWindow`, so rects only drive the policy.

## Add a template
See the retink README, "Add a template". Device part: one `TemplateRenderer` under `templates/<name>/` plus `MIRROR_REGISTER_TEMPLATE`.

## Rebase on upstream
See `MIRROR_CHANGES.md`.
