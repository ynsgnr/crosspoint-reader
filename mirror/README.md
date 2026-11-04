# mirror/: BLE text mirror for the Xteink X4 / X4 Pro

Device side of `ynsgnr/retink`. The Mirror screen (last Home menu item) advertises as `E-Ink Reader` while it is open and
renders the template the phone sends. Security, pairing, the optional keyboard and the trigger chord are described below. Upstream touchpoints: `MIRROR_CHANGES.md`. Findings and unverified items: `docs/FINDINGS.md`.

```
core/        Wire, Envelope, Session (ack/resync/errors), TemplateRenderer + registry, RefreshPolicy, Gfx   (portable)
templates/   chat/: ChatRenderer + ChatLayout                                                               (portable)
device/      MirrorActivity (+ MirrorScreens: unlock, pattern, chord), MirrorBle, MirrorHid, MirrorSettings, GfxAdapter (firmware only)
generated/   written by retink/scripts/sync-fork.sh; MANIFEST.sha256 detects hand edits
sim/ tests/  host simulator (PNG frames) and tests; test/golden/ = transcripts from the Kotlin tests
```

## Build
```bash
pio run -e x4pro-mirror -t upload   # X4 Pro (ESP32-S3)
pio run -e mirror -t upload         # X4 (ESP32-C3)
```
Without `-DCROSSPOINT_MIRROR=1` (every other environment) none of this is compiled.

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
as one block with a thick bar), a fixed 3-line draft area with cursor. Up/Down (or the page buttons) scroll; Back leaves.

## Security (protocol 3)
* **Encrypted and authenticated only.** Every characteristic (mirror `data`, `event`, `caps`, and every HID/battery/device-info characteristic) has `*_ENC | *_AUTHEN`;
  the notification subscription (CCCD) is made `READ/WRITE_ENC|AUTHEN` for the whole GATT server with `ble_gatts_set_clt_cfg_perm_flags` (`MirrorBle.cpp`). A second check in `onWrite`
  drops data from a link that is not encrypted and authenticated. No debug characteristic exists; nothing logs message text.
* **LE Secure Connections only** (`ble_hs_cfg.sm_sc_only`), MITM on, display-only IO: the passkey is shown on the e-ink screen and typed on the phone. Just Works cannot produce an authenticated bond,
  so such a link is disconnected (`onAuthComplete`).
* **Pairing window.** An unknown phone may connect only for 2 minutes after you tap **Pair phone** on the Mirror screen. A bonded phone always may, but cannot re-pair outside the window
  (`onPasskeyDisplay` disconnects). Pairing a new phone deletes the old bond (one phone at a time). Nothing is advertised without a bond or an open window.
* **Privacy.** The public address is used: advertising with the host-based private address failed on the X4 Pro (HCI error 0x12, serial log), so the device can be recognised by its address. It advertises only while the pairing window is open or a bond exists. The advertisement holds the mirror service UUID (plus the HID UUID and appearance when the keyboard is on); the generic name `E-Ink Reader` is in the scan response.
  No content, serial or per-device data. (The mirror service UUID is a project constant; the Companion Device Manager filters on it.)
* **Migration.** `sec_epoch` in NVS (`kSecurityEpoch`); bonds made under an older model are deleted at the next start. The app does the same (`DeviceBond.SECURITY_EPOCH`).

## Controls
The Mirror screen has a column of buttons (`ButtonColumn`): tap them on touch boards (the X4 Pro has no Confirm button by default); on boards without touch, move the cursor with the arrow keys and press Confirm.
Not linked: **Pair phone**, **Forget phone**, **Keyboard on/off**, **Trigger chord**. Linked: **Start mirror on phone**, **Unlock phone**, **Trigger chord**. Forget and Keyboard ask for a second tap because they discard the pairing.
In the chat view the side buttons or a swipe scroll, and tapping the top of the screen (or Confirm) toggles the phone's dark mode. Home / Back leaves the screen you are on.

## Keyboard, unlock and trigger chord (opt-in)
* Off by default. The **Keyboard** button switches the HID keyboard on or off (`MirrorSettings`) and forgets the paired phone, because the keyboard changes the GATT database the phone caches per bond.
  With it on, the same bond and the same pairing carry the mirror and the keyboard; there is no second pairing entry.
* `MirrorHid`: HID-over-GATT keyboard + consumer + system-control report map, HID Information, Protocol Mode, Control Point, Battery, PnP. Built by hand (not `NimBLEHIDDevice`) so all of it needs the authenticated link.
* **Unlock screens** (button **Unlock phone**): opening them wakes the phone first (a Shift tap, then the System Wake Up usage). The PIN pad (1-9, 0, Enter) and the pattern screen work by touch or with the arrow keys + Confirm.
  Pattern: dots 0-8 row by row, each sent as its digit when the finger first touches it (Android's pattern lock reads the string "01258" for 0>1>2>5>8); lifting the finger sends Enter ("Done" does that on boards without touch).
  Every key is sent and released at once; no PIN, pattern, length or buffer exists, and the pattern is not drawn (e-ink would keep it). The phone reports `locked`; the screen closes by itself when it unlocks.
* **Trigger chord**: **Start mirror on phone** (before the phone app runs) sends the chord over HID (default Ctrl+Alt+Shift+M). **Trigger chord** edits it (`core/Chord`: at least two modifiers, no Meta, letter or digit,
  common two-modifier shortcuts refused). The device announces it to the phone (`chord` command) so both sides agree.

## Status band and dark mode
Every template frame has a one-line band on top (`core/Frame.cpp`, `BandedGfx`): `Phone dark mode: tap here`, or `Phone dark mode: N min  (tap here: off)`
while the phone reports it. Tapping the top of the screen (or Confirm) sends the `toggle_dark_mode` command to the phone; the new state comes back as a `status` message.

## Refresh policy
`FAST_REFRESH` (differential, changed pixels only) for partial updates; `HALF_REFRESH` for the first frame, template
switch, resync, and every 20 partials (`RefreshOverrides` per template). Dirty rectangles are computed per paint item; typing
dirties one 24 px line. `HalDisplay` does not expose the SDK's experimental `displayWindow`, so rects only drive the policy.

## Add a template
See the retink README, "Add a template". Device part: one `TemplateRenderer` under `templates/<name>/` plus `MIRROR_REGISTER_TEMPLATE`.

## Rebase on upstream
See `MIRROR_CHANGES.md`.
