# mirror/: BLE text mirror for the Xteink X4 / X4 Pro

Device side of `ynsgnr/retink`. The Mirror screen (last Home menu item) advertises as `CrossPoint Mirror`, pairs
with a passkey shown on screen, and renders the template the phone sends. Advertising and the connection exist only
while the screen is open. Upstream touchpoints: `MIRROR_CHANGES.md`. Findings and unverified items: `docs/FINDINGS.md`.

```
core/        Wire, Envelope, Session (ack/resync/errors), TemplateRenderer + registry, RefreshPolicy, Gfx   (portable)
templates/   chat/: ChatRenderer + ChatLayout                                                               (portable)
device/      MirrorActivity, MirrorBle (NimBLE peripheral), GfxAdapter (GfxRenderer)                        (firmware only)
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

## Refresh policy
`FAST_REFRESH` (differential, changed pixels only) for partial updates; `HALF_REFRESH` for the first frame, template
switch, resync, and every 20 partials (`RefreshOverrides` per template). Dirty rectangles are computed per paint item; typing
dirties one 24 px line. `HalDisplay` does not expose the SDK's experimental `displayWindow`, so rects only drive the policy.

## Add a template
Add `templates/<name>/` with a `TemplateRenderer` subclass and `MIRROR_REGISTER_TEMPLATE(Class, id, minSchema, maxSchema)`;
put the generated `<name>_gen.h` there via `sync-fork.sh`; add the .cpp to `CMakeLists.txt` for host tests.
The firmware picks it up through `build_src_filter` (`+<../mirror/templates/>`).

## Rebase on upstream
See `MIRROR_CHANGES.md`.
