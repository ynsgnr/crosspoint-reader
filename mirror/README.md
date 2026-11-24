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

## Security
* **Encrypted and authenticated only.** Every characteristic (mirror `data`, `event`, `caps`, and every HID/battery/device-info characteristic) has `*_ENC | *_AUTHEN`;
  the notification subscription (CCCD) is made `READ/WRITE_ENC|AUTHEN` for the whole GATT server with `ble_gatts_set_clt_cfg_perm_flags` (`MirrorBle.cpp`). A second check in `onWrite`
  drops data from a link that is not encrypted and authenticated. No debug characteristic exists; nothing logs message text.
* **LE Secure Connections only** (`ble_hs_cfg.sm_sc_only`), MITM on, display-only IO: the passkey is shown on the e-ink screen and typed on the phone. Just Works cannot produce an authenticated bond,
  so such a link is disconnected (`onAuthComplete`).
* **Pairing window.** An unknown phone may connect only for 2 minutes after you tap **Pair phone** on the Mirror screen. A bonded phone always may, but cannot re-pair outside the window
  (`onPasskeyDisplay` disconnects). Pairing a new phone deletes the old bond (one phone at a time). Nothing is advertised without a bond or an open window.
* **Privacy.** The public address is used: advertising with the host-based private address failed on the X4 Pro (HCI error 0x12, serial log), so the device can be recognised by its address. It advertises only while the pairing window is open or a bond exists. The advertisement holds the mirror service UUID plus the HID UUID; the generic name `E-Ink Reader` is in the scan response.
  No content, serial or per-device data. (The mirror service UUID is a project constant; the Companion Device Manager filters on it.)
* **App-level confirmation.** After the bond the phone sends a random 16-bit code (`pair_code`), the device shows it ("Confirm code: 04711") and the user types it into the app; only then does the phone send chat data, and nothing the device sends is acted on before. A device that does not advertise the `pairing_code` feature gets nothing. The confirmation is stored per bond on the phone and cleared when the bond is lost or a new pairing is made.
* **Unsecured links are dropped.** A central that has not become encrypted and authenticated within 5 s (60 s while a pairing is in progress) is disconnected (`core/LinkGuard.h`), so a stranger cannot hold the only slot.
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

## Sync over the phone
**Mirror > Sync books over phone** runs the stock OPDS sync through the phone's mobile data when there is no Wi-Fi to use. It needs the Retink app with "Wi-Fi sync" switched on (`features.wifi_sync`) and a confirmed phone.
1. The device sends `SyncRequest(start)`; the phone opens a LocalOnlyHotspot and a proxy that leads out through its cellular network and answers with `SyncOffer(ready, ssid, passphrase, proxy_host, proxy_port, valid_seconds)` (or a refusal with a reason). The message goes over the encrypted BLE link.
2. The device joins that hotspot (`WiFi.begin`) and turns on the proxy shim, then runs `opdssync::syncNow()`; it finds Wi-Fi already up and skips its own join. Everything it opens (OPDS, downloads, KOReader sync) goes through the proxy.
3. Whatever happens, the shim goes off, Wi-Fi goes off, Bluetooth comes back and the phone is told `done` over the restored link. The phone also stops by itself after `valid_seconds`, or after 60 s without proxy traffic once the BLE link has dropped.
Bluetooth is **off while the sync runs**: the internal heap does not hold Wi-Fi, Bluetooth and a TLS download together (serial log: `OSY Stopping: low memory (free 44540, max block 15860)` with both up, against a 20 KB block needed for a download). The phone keeps its hotspot through the gap (see its `WifiSyncController`).
The sequence is `core/PhoneSync.cpp` (host-tested with a recording fake); the radio, the shim and the OPDS call are `device/MirrorPhoneSync.cpp`.

**The proxy shim** (`device/ProxyHook.cpp`, pure parts in `core/ProxyShim.*`). The firmware's HTTP client opens its own sockets, so instead of changing it (it lives in the FreeInk SDK), the two lwIP calls every connection ends in are wrapped by the linker (`-Wl,--wrap=lwip_connect`, `--wrap=lwip_getaddrinfo`, `platformio_mirror.ini`). While on, a host name gets a fake address (198.18.x.y) and connecting to it, or to any address outside the hotspot, sends `CONNECT name:port` to the proxy and waits for 2xx; TLS then runs through the tunnel as usual. UDP, IPv6 lookups, the proxy and the hotspot subnet are never touched. While off (always, except during this sync) both hooks pass straight through.
**The firmware updater never uses the shim**: `MirrorUpdateActivity` switches it off when it opens and uses the plain Wi-Fi sockets, so a bad build can still fetch its own fix.

## Firmware channels (OTA)
**Home > More > Firmware channels** installs a build that CI published: `develop` (the develop-retink branch) or `pr-<number>` (an open pull request of this repository).
Everything is checked before the running firmware is touched:

1. Wi-Fi, then the list `channels-<board>.txt` and its signature `.sig` from the release `ota` of this repository.
2. The signature (ECDSA P-256 / SHA-256, public key `ota/OtaKey.h`) is checked **before** the list is read (`ota/OtaManifest.cpp`). The list is one line per channel (`<id> <rev> <built> <size> <sha256> <title>`); the device builds the image address itself (`ota/OtaConfig.h`), the list never names a URL.
3. You pick a channel and confirm (Cancel is the default). The battery must be at 25% or more.
4. The image goes to the SD card; its SHA-256 and size must equal the signed list. The existing flasher (`FirmwareFlasher`) then validates the whole image (header, checksum, SHA-256 trailer, chip, board tag) before it erases anything, and only then switches the boot partition.

**Automatic fallback** (`ota/BootGuard.h`, `device/OtaBoot.cpp`). Before it flashes, the installer records "this build is on trial, the old image is in slot X"; if that cannot be saved, nothing is flashed.
Each boot of the build counts a try; one that has run for 10 seconds confirms itself. After 3 boots without confirming, the 4th boot switches back to the old image and restarts. The record names the build's revision, so it never acts on another image, and a record that cannot be read never causes a rollback.
It runs inside the new build, so it is wired in without touching `src/main.cpp`: `device/OtaBootHooks.cpp` defines the Arduino core's `initVariant()` (before `setup()`), and the link flag `-Wl,--wrap=_Z4loopv` in `platformio_mirror.ini` ticks it before every `loop()` (a missing flag is a link error). CI builds both; neither has been run on a device yet.
The fallback is custom because ESP-IDF's own rollback needs a different bootloader, which these devices do not get over OTA. What it does not cover:
* **Short starts.** Every boot counts, wake from sleep included: three starts that each end within 10 seconds roll a good build back. After an install, let the device run 10 seconds before switching it off.
* **Failures before `setup()`** (static initialisation, the bootloader), and **a build that runs 10 seconds and then misbehaves**: recover by hand (below).
* **The first install**, and installs by the **other update paths** (SD card, stock OTA, web flasher): the guard protects an install only if the installing and the installed build both contain it. A record left from an earlier trial is ignored once a different build runs.
* **No old image to go back to.** The trial just ends; only the log says so. The last failed boot's crash report is not saved (the earlier ones are, in `/crash_report.txt`).
* **Flash writes are bounded:** NVS is written at most once to arm, 3 times during the trial and once to end it; a rollback whose switch keeps failing is tried 3 times. Nothing retries by itself: an install, the SD recovery and the stock update each write only after you confirm.

**Recovery by hand.** Do this now, on a healthy device; it has not been tested on one yet.
1. Get a known-good image: Actions > a green `mirror` run on `develop-retink` > Artifacts > `firmware-x4pro-mirror.bin` (needs a GitHub login; artifacts expire after 90 days, so keep a copy). Unzip it: it contains `firmware.bin`.
2. Copy `firmware.bin` to the root of the SD card.
3. With the device fully off, hold **Down** (X4 Pro) and press the power button. The recovery screen opens a file browser at the card root showing only `.bin` files; choose `firmware.bin` and confirm.
This runs early in `setup()`, so it works unless the build fails before that point; then use the USB web flasher.

**Publishing.** `.github/workflows/ota-publish.yml` runs `mirror/scripts/ota/publish.sh` for every push to develop-retink and every pull request into it. It runs from the base branch (`pull_request_target`) and never runs pull request code: the ordinary `mirror` workflow builds the firmware without secrets; the publisher waits for that build,
adds the image and one line to the list, signs the list with `openssl` and uploads all three to the release `ota`. A channel disappears when its pull request is no longer an open one of this repository (checked on every run). A fork's pull request is never published. A failed GitHub call stops the script before anything changes. Images the list no longer names are deleted last.
What this keeps simple, on purpose: every open pull request of this repository gets a channel (write access to the repository is the only gate, and you still pick and confirm on the device); the device does not refuse an older signed list (its images would no longer match the hashes anyway); the list and its signature are two uploads, so a device may once see a mismatch and just retries.
If GitHub drops a waiting run (it keeps one per concurrency group), re-run the cancelled run in the Actions tab. The repository address is fixed in `ota/OtaConfig.h`; `mirror/scripts/ota/test.sh` fails when it, or the board name, stops agreeing with the script.
The `mirror` workflow fails a build whose `MIRROR_GIT_REV` is not its commit (and so, publishes nothing), because the fallback only acts on an image that reports the revision the list names. `mirror/scripts/ota/test.sh` checks the list handling and the signing, and keeps `test/ota/channels-x4pro.txt` (which the host tests also read) equal to what the script writes.

One-time setup (repository owner):
1. Settings > Environments > New environment `ota-signing`, deployment branches: only `develop-retink`. A pull request's own workflow can then not read the key.
2. Add its secret `OTA_SIGNING_KEY`: the private key that matches `ota/OtaKey.h`, e.g. `gh secret set OTA_SIGNING_KEY --env ota-signing < mirror/ota-signing.pem` (the file is git-ignored). The script refuses any other key. Keep a copy outside the repository: without it you need a new pair (`mirror/scripts/ota-keygen.sh`), a new `OtaKey.h` and a flash of every device by SD card or web flasher.
3. Push to develop-retink once (a change under `mirror/` runs the build), so the `develop` channel exists. A pull request needs a green `mirror` build, so it must change files that workflow watches (`mirror/**`, `platformio.ini`, the Home files).

## Refresh policy
`FAST_REFRESH` (differential, changed pixels only) for partial updates; `HALF_REFRESH` for the first frame, template
switch, resync, and every 20 partials (`RefreshOverrides` per template). Dirty rectangles are computed per paint item; typing
dirties one 24 px line. `HalDisplay` does not expose the SDK's experimental `displayWindow`, so rects only drive the policy.

## Add a template
See the retink README, "Add a template". Device part: one `TemplateRenderer` under `templates/<name>/` plus `MIRROR_REGISTER_TEMPLATE`.

## Rebase on upstream
See `MIRROR_CHANGES.md`.

CI: the `mirror` workflow builds `x4pro-macro` (artifact `firmware-x4pro-mirror.bin`) for PRs that touch `mirror/`, `platformio.ini` or the Home files.

Wi-Fi: background syncs (TRMNL sleep image, OPDS) try the last connected network first (no scan) and scan for the other saved networks only when that fails.
