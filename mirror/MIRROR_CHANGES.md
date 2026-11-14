# MIRROR_CHANGES: upstream files touched by the mirror feature

Everything else lives under `mirror/` (new files only). Without `-DCROSSPOINT_MIRROR=1` the firmware is the
same as upstream: the hooks below are `if (MIRROR_ENABLED)` / `#if MIRROR_ENABLED` guarded (`MIRROR_ENABLED` is 0 without
the flag) and no mirror header is included, so non-mirror environments need no extra include path.
Only the `mirror` and `x4pro-mirror` PlatformIO environments set the flag.

To check the list is complete after a rebase: `git diff --stat upstream/develop -- . ':!mirror'` must show only the files below.

| Upstream file | Change | Why |
| --- | --- | --- |
| `platformio.ini` | `extra_configs` is now a two-line list that also names `mirror/platformio_mirror.ini` | Defines the `mirror` and `x4pro-mirror` environments (feature flag, NimBLE role flags, `mirror/build.py` that compiles `mirror/`). All build configuration lives in that file. |
| `src/activities/home/HomeActivity.h` | two method declarations, `onMirrorOpen()` and `onMoreOpen()` | Home menu entry handlers |
| `src/activities/home/HomeActivity.cpp` | a local `#ifdef CROSSPOINT_MIRROR` block (defines `MIRROR_ENABLED`, includes `device/MirrorActivity.h`, `device/MoreMenuActivity.h`). In mirror builds the **list** Home shows Library, Sync Now (when set up), Mirror and "More..." only: `getMenuItemCount()`, `activateSelection`, the item list in `render()` and the initial selection are switched by `MIRROR_ENABLED && !coverGridUi`. The cover-grid Home keeps its tabs (Mirror is its last one). Stock builds compile the original code. | The list Home cannot scroll, so a 7th row fell below the screen; File Browser, File Transfer, OPDS/Plugins and Settings moved under "More..." (`mirror/device/MoreMenuActivity`) |
| `src/main.cpp` | an `#include` and two calls, each in `#if defined(CROSSPOINT_MIRROR)`: `bootGuardAtStart()` at the top of `setup()` and `bootGuardTick()` at the top of `loop()` | Automatic fallback of a freshly installed OTA channel build (`mirror/ota/BootGuard.h`): counts boots of a build on trial and, when it never confirms, switches back to the old image. Stock builds compile the original code |
| `src/components/CoverGridHomeUi.cpp`, `.h` | the same 5-line `MIRROR_ENABLED` block; tab loop 6 -> 7 slots, one icon-table entry, icon index mapping by "present" table, `tabItems` array size 7 | Same Home entry for the cover-grid theme (the X4 Pro default theme needs PSRAM) |

Outside `mirror/` and the table above: `.github/workflows/ota-publish.yml` and `scripts/ota/` (new: signed OTA channels, see `mirror/README.md`), `.github/workflows/mirror.yml` (new: host tests, manifest check, spec sync, the X4 Pro mirror build; the X4 build on manual dispatch; builds `develop-retink` pushes, which feed the `develop` OTA channel; the `OTA scripts` job), `ci.yml` (the fork builds only the X4 Pro and cancels superseded runs) and `pr-firmware-links.yml` (expects only `firmware-x4pro.bin`). `release.yml` and `release_candidate.yml` still build every board on purpose.

Not touched: `ActivityManager`, translations (`lib/I18n`: the menu label "Mirror" is a plain string, not translated),
`partitions*.csv`, `lib/BlePageTurner`, the SDK submodule, any `sdkconfig`.

## BLE stack: what the build already had and what the mirror changes

* Upstream already builds NimBLE-Arduino 2.5.1 (the SDK's `BleKeyboardHost`, central only; `lib/BlePageTurner`).
  Both NimBLE roles the mirror needs are compiled out by `-DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED=1` and
  `-DCONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED=1` (in `[ble_page_turner] build_flags`).
  `mirror/platformio_mirror.ini` re-enables them for the mirror environments with `-U...` (a later `-U` wins), so no
  upstream config line changes.
* Coexistence with the page turner: `ActivityManager` calls `bleturner::beforeScreenChange()` before every screen
  change, and the Mirror screen is not a reader or Home, so the page turner keeps its radio off while Mirror is open.
  When Mirror closes it calls `NimBLEDevice::deinit(true)`; the page turner starts its own stack again by its normal rules.
* MTU: requested at runtime (`NimBLEDevice::setMTU(247)`), not through sdkconfig, because the C3 core rebuild bakes
  `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=23` into `sdkconfig.h`.

## Rebasing on upstream

1. `git fetch upstream && git rebase upstream/develop` (or merge).
2. Conflicts can only occur in the files above. Re-apply the hook by hand; each is a few lines. Search for
   `MIRROR_ENABLED` and `Mirror` to find them.
3. `git diff --stat upstream/develop -- . ':!mirror'` should list exactly the files in the table.
4. Build both environments: `pio run -e mirror` and `pio run -e x4pro-mirror`; run the host tests (`mirror/README.md`).
5. If upstream changed `HomeActivity` menu handling (for example, a new trailing item), keep Mirror as the last item
   and keep the Sync check at `count - 1 - MIRROR_ENABLED`.

## Known gaps / not verified on hardware

See `mirror/docs/FINDINGS.md`, section "Not verified".
