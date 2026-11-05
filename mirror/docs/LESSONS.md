# Lessons learned

Short rules from bugs found on real hardware (X4 Pro, ESP32-S3, NimBLE-Arduino 2.x, Pixel with Android 16).
Each line: what happened, what to do. Details are in `FINDINGS.md`.

## Pairing and security
* **Android decides the pairing dialog from the device class.** A keyboard appearance (0x03C1) in the advertisement makes the phone class the device as a keyboard; it then invents its own PIN and answers the passkey request itself. Advertise the HID service UUID, never the appearance.
* **Bond first, associate second.** A companion association makes Android skip the pairing dialog for a while. The app bonds (user types the code), then asks for the association.
* **Never delete a bond because a reconnect failed.** Only a pairing in progress may delete a weak bond. A failed encryption (MIC failure) once erased the pairing and forced a manual re-pair.
* **Do not start link-layer procedures at connect.** Connection update and data length at connect time overlapped the phone's encryption start. Wait for the secure link. Do not send your own security request: Android secures the link itself.

## NimBLE lifetime
* **Callback objects outlive the stack.** NimBLE keeps raw pointers to them. Point them at the current owner on every start and unbind on stop; a stale owner crashed the second visit to the screen (`std::mutex::lock` abort).
* **Wait for every link to close before `deinit`,** and stop the stack in the owner's destructor.
* **A connected phone cannot see a device that stops advertising.** Keep a non-connectable advertisement while linked so the companion scan and presence detection still work. A connectable one invites a second connection that is refused and retried forever.
* **Disconnect events of refused links must not touch the accepted link's state:** key them by connection handle.
* **A settings read that fails must not wipe state.** The security-epoch check deleted every bond whenever NVS could not be read; only a confirmed stale value may delete, and the new epoch is recorded after the delete.
* **Never return a fixed passkey to refuse a pairing** (the stack pairs with it before the disconnect lands), and let a pairing that started inside the window finish after it closes.
* **Never block the activity loop while it holds the lock** (waits, key taps with delays): defer with a deadline and let the loop send it.

## Android
* **Reconnect with `autoConnect`** after the first drop; polling with backoff misses the moment the device returns. A companion presence service starts the foreground service when the device appears.
* **Status text that is redrawn is easy to miss:** show progress and errors as a toast too.
* **A cleanup that triggers a state change must not overwrite the state that caused it** (NEEDS_REPAIR was erased by the disconnect it started).
* **A dim overlay over the whole screen can lock the phone up;** change only the system brightness and end it when the phone locks.

## Parsing chat apps
* **Prefer a structural signal over a position fraction.** WhatsApp's delivery tick marks outgoing rows; wide bubbles start left of any cutoff.
* **Clock times need a day.** Read the date separators ("Today", weekday, date); the rows above the first separator belong to the day before it.
* **Capture first, then fix:** export a capture from the phone and run the real parser on it (`parserDump`) before changing a parser.
* **A cleanup that triggers a state change must not overwrite the state that caused it** (NEEDS_REPAIR was erased by the disconnect it started). Read failures before treating a state as final:** a cleanup that triggers a state change must not overwrite the state that caused it (NEEDS_REPAIR was erased by the disconnect it started).
* **Mirror only the message composer, never "any editable field":** search boxes and login fields are editable too.

## Debugging and process
* **Decode panics.** Rebuild the flashed commit, run `xtensa-esp32s3-elf-addr2line` on the backtrace. The build time on the Mirror screen says which commit is running.
* **Read the platform source** (Android Bluetooth stack on googlesource) instead of guessing; it answered the pairing question in minutes.
* **Log what decides things:** peer address, bond count, auth result, disconnect reason code. Never log keys or PINs.
* **CI:** build once per pull request (push builds only `develop`), cancel superseded runs, build only the board you flash.
