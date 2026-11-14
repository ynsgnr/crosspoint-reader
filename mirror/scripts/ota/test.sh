#!/usr/bin/env bash
# Checks the list handling and the signing of publish.sh without GitHub, and keeps the fixture that the C++ host tests read
# (mirror/test/ota/channels-x4pro.txt) equal to what publish.sh writes:   mirror/scripts/ota/test.sh
# After changing the list format: UPDATE_FIXTURE=1 mirror/scripts/ota/test.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/publish.sh"
FIXTURE="$ROOT/mirror/test/ota/channels-x4pro.txt"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
SHA=$(printf 'a%.0s' $(seq 64))

fail() { echo "FAIL: $*" >&2; exit 1; }
ids() { cut -d' ' -f1 "$1" | paste -sd' ' -; }
expect() { [ "$2" = "$3" ] || fail "$1: got '$2', want '$3'"; }

# --- the list ---
: > "$T/list"
list_set "$T/list" pr-7 b0b0b0b0 1763000000 600000 "$SHA" "Older pull request"
list_set "$T/list" develop 97f15374 1763100001 600000 "$SHA" "develop-retink: Integration branch"
list_set "$T/list" pr-19 a16f221d 1763100000 600000 "$SHA" "List template on the device"
[ "${UPDATE_FIXTURE:-}" = 1 ] && cp "$T/list" "$FIXTURE"
cmp -s "$T/list" "$FIXTURE" || fail "the fixture differs from what publish.sh writes (UPDATE_FIXTURE=1 rewrites it)"
expect "order: develop first, then pull requests newest first" "$(ids "$T/list")" "develop pr-19 pr-7"

list_set "$T/list" pr-19 bbbbbbbb 1763200000 600000 "$SHA" "Same channel again"
expect "a channel is replaced, not added twice" "$(ids "$T/list")" "develop pr-19 pr-7"
expect "...with the new revision" "$(grep '^pr-19 ' "$T/list" | cut -d' ' -f2)" "bbbbbbbb"
list_drop "$T/list" pr-7
expect "drop" "$(ids "$T/list")" "develop pr-19"

: > "$T/many"
for n in 1 2 3 4 5 6 7 8 9 10; do list_set "$T/many" "pr-$n" a16f221d 1763100000 600000 "$SHA" "PR $n"; done
list_set "$T/many" develop 97f15374 1763100000 600000 "$SHA" "develop"
expect "at most $MAX_CHANNELS channels, develop kept, the oldest pull requests go" "$(ids "$T/many")" "develop pr-10 pr-9 pr-8 pr-7 pr-6 pr-5 pr-4"

: > "$T/titles"
list_set "$T/titles" pr-1 a16f221d 1 600000 "$SHA" $'  tab\there \x01 and   spaces \xC3\xA9  '
list_set "$T/titles" pr-2 a16f221d 1 600000 "$SHA" ""
list_set "$T/titles" pr-3 a16f221d 1 600000 "$SHA" "$(printf 'x%.0s' $(seq 200))"
expect "a title is one clean printable line" "$(grep '^pr-1 ' "$T/titles" | cut -d' ' -f6-)" "tabhere and spaces"
expect "an empty title" "$(grep '^pr-2 ' "$T/titles" | cut -d' ' -f6-)" "untitled"
expect "a long title is cut" "$(grep '^pr-3 ' "$T/titles" | cut -d' ' -f6- | tr -d '\n' | wc -c | tr -d ' ')" "96"

# --- signing ---
"$HERE/../ota-keygen.sh" "$T/key.pem" > "$T/OtaKey.h" 2>/dev/null
openssl ec -in "$T/key.pem" -pubout -out "$T/pub.pem" 2>/dev/null
sign "$T/key.pem" "$T/list"
openssl dgst -sha256 -verify "$T/pub.pem" -signature "$T/list.sig" "$T/list" >/dev/null || fail "a fresh signature does not verify"
[ "$(stat -c%s "$T/list.sig")" -le 72 ] || fail "the signature does not fit the device's 128 byte limit"
cp "$T/list" "$T/tampered"; printf ' ' >> "$T/tampered"
openssl dgst -sha256 -verify "$T/pub.pem" -signature "$T/list.sig" "$T/tampered" >/dev/null 2>&1 && fail "a changed list still verifies"

key_matches_firmware "$T/key.pem" "$T/OtaKey.h" || fail "the key does not match its own public key array"
openssl ecparam -name prime256v1 -genkey -noout -out "$T/other.pem"
key_matches_firmware "$T/other.pem" "$T/OtaKey.h" && fail "another key was accepted"

# --- the names the firmware and CI both use ---
grep -q "CROSSPOINT_BOARD_NAME \"$BOARD\"" "$ROOT/src/network/FirmwareBoardTag.cpp" || fail "BOARD=$BOARD is not a board name of the firmware"
grep -q "kReleaseBase\[\] = \"https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/\"" "$ROOT/mirror/ota/OtaConfig.h" || fail "the release address in OtaConfig.h changed: keep CI and the firmware in step"

echo "ota scripts: ok"
