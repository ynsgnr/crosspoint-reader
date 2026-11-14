# mirror/: BLE text mirror for the Xteink X4 / X4 Pro

Device side of `ynsgnr/retink`. The Mirror screen (last Home menu item) advertises as `E-Ink Reader` while it is open and
renders the template the phone sends. Security, pairing, the keyboard, the app launcher and the trigger chord are described below. Upstream touchpoints: `MIRROR_CHANGES.md`. Findings and unverified items: `docs/FINDINGS.md`. Lessons from the hardware bugs: `docs/LESSONS.md`.

```
core/        Wire, Envelope, Session (ack/resync/errors), TemplateRenderer + registry, RefreshPolicy, Gfx   (portable)
templates/   chat/: ChatRenderer + ChatLayout; list/: ListRenderer + ListLayout                             (portable)
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

The status page's bottom line shows the build: the commit (a PR build shows the PR's head commit; `+` = uncommitted changes) and when it was compiled. `mirror/build.py` writes it to `mirror/device/BuildInfo.h` (not committed).

First pairing: open Mirror and tap Pair phone, then in the Retink app tap "2. Pair e-ink device" and type the 6-digit code shown here. After that the phone reconnects by itself whenever Mirror is open.

`generated/` and `test/golden/` come from the Android repo (`ynsgnr/retink`): run its `scripts/sync-fork.sh` with this repo at `../crosspoint-reader`; never edit them by hand.

## Emoji (optional font)
The built-in UI fonts have no emoji, so chats show a placeholder for them. `fonts/LibroEmoji/` is an SD-card font with emoji:
1. Copy the `fonts/LibroEmoji` folder to `/fonts/` on the SD card (or upload it through the device's web interface).
2. On the device: **Settings > Reader > Font Family > LibroEmoji**.

It is also your reading font from then on. Chat lines with an emoji are drawn in it, lines without keep the built-in UI font
(the firmware's UI fallback is per string). The firmware draws one glyph per character, so the phone simplifies emoji
built from several (skin tone dropped, a joined emoji keeps its first part, a flag becomes its letters). Rebuild with
`fonts/build-emoji-font.sh`.

LibroEmoji is a modified version of [Libron](https://github.com/nicoverbruggen/libron) by Nico Verbruggen (based on Readerly
and Newsreader) with Google's [Noto Emoji](https://github.com/google/fonts/tree/main/ofl/notoemoji) as the fallback for
the characters Libron lacks. Both are licensed under the SIL Open Font License 1.1; the font files stay under that license
(`fonts/LibroEmoji/LICENSE-*.txt`, changes in `FONTLOG.txt`). It is not an official Libron release, and it does not use
the reserved name "Libron".

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
as one block with a thick bar; an image or other non-text item, sent as U+FFFC, is a small marker: a line with a tiny "v", three dots, a line
with a tiny "^"), and the draft box with its cursor. The box is 3 lines tall and grows with the text (up to most of the
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

## List template
Rows of a chat app's conversation list (label, preview, time, unread badge); the selected row is shown inverted and the view scrolls to keep it visible.
The rows share the whole height under the title (`rowHeightFor`, clamped to 1-4 times the natural row height), so a short list leaves no empty band below it.
The device names a row by id only. Up/Down (and Left/Right, swipes, held buttons) move the highlight one row at once and send Chat's scroll events
(`core/ScrollButtons.h` is shared with the chat); pressing on at the first or last held row makes the phone scroll its list, and the rows it brings into view arrive as diffs.
Confirm or a tap on a row sends `Activate(id)` and the phone touches that row on its own screen (a tap is offered to the template first, so it only toggles dark mode
outside the rows; `TemplateRenderer::onTap`, `Session::onTap`). Opening the conversation brings the chat template to the front.

## View stack and Back
`Session` keeps the templates the device showed (`canGoBack()` while more than one: a chat opened from the list is list, chat). A switch to a template already in
the stack pops to it, like Android's back stack. On a template screen Back always sends the `back` command first (`Session::goBack`): the phone performs its own back
action, the earlier screen comes to the front (the stack pops, or the same template shows what the app went back to). When the phone cannot go back any further (the
app left to the launcher, or there was nothing to go back to) it sends `back_unavailable` (`Session::takeBackUnavailable`) and the device opens its menu, the launcher.
A locked or silent phone cannot answer, so Back opens the menu at once. The band says `Back: previous` while a view lies below.

## Locked phone
While the phone says it is locked, a template screen is replaced by the PIN pad (it wakes the phone, like **Unlock phone**), so the last app is never shown over a lock screen. Back from the pad opens
the status page (Unlock phone, Forget phone...); the pad does not reopen until the phone locks again or **Back to chat** is chosen while it is still locked. The pad closes by itself when the phone unlocks.

## Firmware channels (OTA)
**Home > More > Firmware channels** installs a build CI published: `develop` (the develop-retink branch) or `pr-<number>` (an open pull request of this repository that carries the label `ota`).
The device trusts only what the fork's OTA key signed, and every step before the flash can fail without touching the running firmware:

1. Wi-Fi, then `channels.json` and `channels.json.sig` from the release `ota` of this repository (`ota/OtaConfig.h`).
2. The signature (ECDSA P-256 / SHA-256, `kOtaPublicKeyDer` in `ota/OtaKey.h`) is checked **before** the file is parsed. Then the device's own policy (`ota/OtaManifest.cpp`): only this board, only file names under the release's URL, a plausible size,
   lower-case SHA-256, at most 8 channels, a manifest of at most 8 KiB (the publisher stays under 6 KiB), and a manifest older than one already accepted is a replay and is refused.
3. You choose a channel and confirm (Cancel is the default). The battery must be at 25% or more.
4. The image goes to the SD card, its SHA-256 and size must equal the signed manifest's, then `FirmwareFlasher` validates it (header, checksum, SHA-256 trailer, chip, board tag) before it erases anything, and only then switches the boot partition.

**Automatic fallback.** Before it flashes, the installer records that the new build is *on trial* and which slot holds the running image (`ota/BootGuard.h`, NVS key `ota_guard`); if that cannot be saved, nothing is flashed.
Every boot of that build counts a try (before `setup()` runs, `device/OtaBoot.cpp`); a build whose main loop has run for 10 seconds confirms itself. A build that has not confirmed after 3 boots is replaced by the old image on the 4th: the boot partition is switched back and the device restarts.
The Firmware channels screen says when the last build was rolled back.
How it is wired in without editing upstream files (`src/main.cpp` is untouched): `device/OtaBootHooks.cpp` defines the Arduino core's weak `initVariant()` hook, which runs after NVS is initialised and before `setup()`, and the link flag `-Wl,--wrap=_Z4loopv` in `platformio_mirror.ini` routes the core's call of `loop()` through a tick that runs before every pass. A missing flag is a link error, not a silent loss.
Safety of the guard itself: the record names the revision of the build on trial, so it never acts on another image; a damaged or unreadable record never causes a rollback; and a rollback only happens if the old slot still holds an image for this chip. The guard depends on the revision the image reports (`MIRROR_GIT_REV`) being exactly the first 8 characters of its commit, which is what the manifest names:
the `mirror` workflow fails the build if they differ, and the publisher checks the file again.

What it does not cover:
* **Starts too short.** Every boot counts, including a wake from sleep. Three starts that each end (power-off or sleep) within 10 seconds count as failures and roll a good build back. After an install, let the device run for 10 seconds before switching it off.
* **Failures before `setup()`** (static initialisation, the bootloader) and **a build that runs for 10 seconds and then misbehaves**: the guard runs inside the new build and cannot see them. Use the manual recovery below.
* **The first install.** It protects an install only if the build doing the installing and the build being installed both contain it. Installing the first guard-bearing build from an older one is unprotected.
* **Other update paths** (the SD-card update, the stock OTA, the web flasher) do not arm the guard. A record left from an earlier trial is ignored when a different build runs, and dropped. One corner: if such an update is cut short by a power loss after it began overwriting the old slot, a later rollback could land on a damaged image. The guard checks only the image header.
* **Flash writes are bounded.** The guard writes NVS at most once to arm, 3 times during the trial and once to confirm or roll back; a rollback whose switch keeps failing is tried 3 times and then given up. Nothing in the update paths retries by itself: an install, the SD recovery and the stock update each write only after you confirm, and run once.
* **No old image to go back to.** Then the trial just ends and the new build keeps running; only the log says so.
* **The crash report** of the last failed boot is not written when the device rolls back (the restart clears it). The reports of the earlier failed boots are: each boot saves the previous boot's panic to `/crash_report.txt`.
It is custom, in the app, because ESP-IDF's own rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) needs a different bootloader, and the bootloader of these devices is not updated over OTA.

**Recovery by hand.** Do this *now*, on a healthy device, before you rely on channel updates; it has not been tested on a device yet.
1. Get a known-good image: Actions > a green `mirror` run on `develop-retink` > Artifacts > `firmware-x4pro-mirror.bin` (needs a GitHub login; runs and their artifacts expire after 90 days, so keep a copy). Unzip it: it contains `firmware.bin`.
2. Copy `firmware.bin` to the root of the SD card.
3. With the device fully off, hold **Down** (X4 Pro) and press the power button. The recovery screen opens a file browser at the card root, showing only `.bin` files (open folders to find yours); choose `firmware.bin` and confirm.
This runs early in `setup()`, so it works unless the build fails before that point. Then use the USB web flasher.

**Publishing** (`.github/workflows/ota-publish.yml`, `mirror/scripts/ota/`). It runs from the base branch (`pull_request_target`) and never runs pull request code: the PR's firmware is built by the ordinary `mirror` workflow; the publisher waits for that run, hashes the image,
rewrites `channels.json` (`mirror/scripts/ota/channels.py`), signs it with `openssl` and uploads image, manifest and signature to the release `ota`. A channel disappears when its PR is closed or loses the label (every run sweeps stale ones).
Pull requests from forks are never published. `mirror/scripts/ota/test_channels.py` checks the tool, the manifest fixture that the host tests also read (`mirror/test/ota/`), and the openssl signature round trip.
The manifest rules exist twice, in `mirror/ota/OtaManifest.cpp` (what the device accepts) and `mirror/scripts/ota/channels.py` (what CI writes); a change to either needs the other and the fixture (`python3 mirror/scripts/ota/test_channels.py --write-fixture`).
A failed GitHub call stops the publisher before it changes anything, so a transient error can never shorten the manifest. GitHub keeps only one waiting run per concurrency group, so a publish can be dropped; the manual run of the workflow (*Run workflow*) is a reconcile that adds whatever is missing or out of date.

One-time setup (repository owner):
1. Settings > Environments > New environment `ota-signing`; Deployment branches: *Selected branches* > `develop-retink`. A pull request branch can then not read the key.
2. Add the environment secret `OTA_SIGNING_KEY`: the PEM private key that belongs to the public key in `ota/OtaKey.h`, e.g. `gh secret set OTA_SIGNING_KEY --env ota-signing < ota-signing.pem`. The publisher refuses to sign with any other key.
3. Keep a backup of the private key outside the repository (`mirror/ota-signing.pem` is git-ignored; never commit it). Losing it means a new pair and a firmware update by SD card or web flasher before channels work again.
4. Create the label `ota` once (Issues > Labels). A pull request that carries it gets a `pr-<number>` channel.
5. Seed the `develop` channel: the next push to `develop-retink` that touches files the `mirror` workflow watches publishes it (or run the workflow by hand once it exists on the default branch). Until then the screen says "No channel list is published yet".

A pull request channel needs a green `mirror` build of the PR head, so the PR must change files that workflow watches (`mirror/**`, `platformio.ini`, the Home files); the publisher waits up to 30 minutes for that build. A first-time contributor's workflow runs may need approval.
To rotate the key: `mirror/scripts/ota-keygen.sh`, replace `kOtaPublicKeyDer` in `ota/OtaKey.h`, update the secret, and put the new firmware on every device by SD card or web flasher: devices still holding the old key accept nothing signed by the new one.

## Refresh policy
`FAST_REFRESH` (differential, changed pixels only) for partial updates; `HALF_REFRESH` for the first frame, template
switch, resync, and every 20 partials (`RefreshOverrides` per template). Dirty rectangles are computed per paint item; typing
dirties one 24 px line. `HalDisplay` does not expose the SDK's experimental `displayWindow`, so rects only drive the policy.

## Add a template
See the retink README, "Add a template". Device part: one `TemplateRenderer` under `templates/<name>/` plus `MIRROR_REGISTER_TEMPLATE`.

## Rebase on upstream
See `MIRROR_CHANGES.md`.

CI: the `mirror` workflow builds `x4pro-macro` (artifact `firmware-x4pro-mirror.bin`) for PRs that touch `mirror/`, `platformio.ini` or the Home files.

Wi-Fi: background syncs (TRMNL sleep image, OPDS) join any saved network in range, last connected first.
