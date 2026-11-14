#!/usr/bin/env bash
# Publishes the signed OTA channels to the GitHub release "ota" (called by .github/workflows/ota-publish.yml).
#
# The release holds, per board:   channels-<board>.txt       the list (format: mirror/ota/OtaManifest.h)
#                                 channels-<board>.txt.sig   its ECDSA P-256 signature
#                                 <id>-<board>.bin           one image per channel (id: "develop" or "pr-<number>")
# Environment: GH_TOKEN REPO OTA_SIGNING_KEY, and for the event that started the run: EVENT (push or pull_request_target),
# SHA (the commit to publish), PR and TITLE (the pull request's number and title).
# Nothing from a pull request is run here: its firmware is downloaded, hashed and listed as opaque data.
# Any failed GitHub call stops the script before anything is changed.
set -euo pipefail

BOARD=x4pro
ARTIFACT=firmware-x4pro-mirror.bin   # what the "mirror" workflow uploads
BRANCH=develop-retink                # the "develop" channel, and the base branch of the pull requests that get a channel
MAX_CHANNELS=8
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"   # the repository root
LIST="channels-$BOARD.txt"

log() { echo "ota: $*"; }

# ---- the list: "<id> <rev> <built> <size> <sha256> <title>", develop first, then pull requests newest first ----

# stdin -> stdout, in that order, at most $MAX_CHANNELS lines
list_order() {
  local all
  all=$(cat)
  { grep '^develop ' <<<"$all" || true; { grep '^pr-' <<<"$all" || true; } | sort -t' ' -k1.4,1.9nr; } | sed -n "1,${MAX_CHANNELS}p"
}

# list_set FILE id rev built size sha256 title: adds the channel, or replaces the one with the same id
list_set() {
  local file=$1 id=$2 title
  title=$(printf '%s' "$7" | LC_ALL=C tr -cd '[:print:]' | tr -s ' ' | sed 's/^ //; s/ $//' | cut -c1-96)
  { { grep -v "^$id " "$file" || true; } ; printf '%s %s %s %s %s %s\n' "$id" "$3" "$4" "$5" "$6" "${title:-untitled}"; } | list_order > "$file.new"
  mv "$file.new" "$file"
}

# list_drop FILE id
list_drop() {
  { grep -v "^$2 " "$1" || true; } > "$1.new"
  mv "$1.new" "$1"
}

# ---- signing ----

hex_of_key_array() { grep -o '0x[0-9a-fA-F][0-9a-fA-F]' "$1" | cut -c3-4 | tr -d '\n' | tr 'A-F' 'a-f'; }

# The signing key must be the one compiled into the firmware (mirror/ota/OtaKey.h), or no device would accept the result.
key_matches_firmware() {   # KEY [HEADER]
  [ "$(openssl ec -in "$1" -pubout -outform DER 2>/dev/null | od -An -v -tx1 | tr -d ' \n')" = "$(hex_of_key_array "${2:-$ROOT/mirror/ota/OtaKey.h}")" ]
}

# sign KEY FILE: writes FILE.sig (DER), the format the device checks
sign() {
  openssl dgst -sha256 -sign "$1" -out "$2.sig" "$2"
}

# ---- the release ----

# The channels whose pull request is open and from this repository: "pr-<number>" per line.
open_pull_requests() {
  gh pr list -R "$REPO" --base "$BRANCH" --state open --json number,isCrossRepository \
    --jq '.[] | select(.isCrossRepository | not) | "pr-\(.number)"'
}

# Removes the channels of pull requests that are no longer open ones of this repository ($LIVE); the list is checked on every
# run, so a missed event heals.
drop_closed() {
  local id
  for id in $(grep -o '^pr-[0-9]*' "$WORK/$LIST" || true); do
    grep -qx "$id" <<<"$LIVE" && continue
    log "removing $id (closed)"
    list_drop "$WORK/$LIST" "$id"
    gh release delete-asset ota "$id-$BOARD.bin" -R "$REPO" -y 2>/dev/null || true
    changed=1
  done
}

# Adds or replaces channel $1 from the image the "mirror" workflow built for commit $2 ($3 is the title).
publish_channel() {
  local id=$1 sha=$2 title=$3 run image
  [[ "$id" =~ ^(develop|pr-[1-9][0-9]{0,5})$ && "$sha" =~ ^[0-9a-f]{40}$ ]] || { log "bad channel or commit"; exit 1; }
  # Wait for the build of exactly this commit (the pull request's own run), and publish only a green one.
  for _ in $(seq 1 60); do
    run=$(gh run list -R "$REPO" --workflow mirror.yml --commit "$sha" --json databaseId,status,conclusion --limit 10 \
      --jq 'map(select(.status == "completed")) | first | if . == null then "" else "\(.databaseId) \(.conclusion)" end')
    [ -n "$run" ] && break
    sleep "${POLL_SECONDS:-30}"   # (the test shortens it)
  done
  [ -n "$run" ] || { log "no finished build for $sha: $id not published"; return 0; }
  [ "${run#* }" = success ] || { log "the build for $sha ended '${run#* }': $id not published"; return 0; }

  mkdir -p "$WORK/download"
  gh run download "${run% *}" -R "$REPO" -n "$ARTIFACT" -D "$WORK/download"
  image="$WORK/$id-$BOARD.bin"
  mv "$WORK/download/firmware.bin" "$image"
  list_set "$WORK/$LIST" "$id" "${sha:0:8}" "$(date +%s)" "$(stat -c%s "$image")" "$(sha256sum "$image" | cut -d' ' -f1)" "$title"
  gh release upload ota "$image" -R "$REPO" --clobber   # the image first: the list never names a missing file
  changed=1
}

main() {
  WORK=$(mktemp -d)
  trap 'rm -rf "$WORK"' EXIT
  changed=0

  gh release view ota -R "$REPO" >/dev/null 2>&1 || gh release create ota -R "$REPO" --prerelease --latest=false \
    --title "OTA channels" --notes "Signed firmware builds for the Firmware channels screen. Written by CI; do not edit."
  # Start from an empty list only when the release has none. One that exists but cannot be downloaded stops the run.
  if [ "$(gh release view ota -R "$REPO" --json assets --jq "[.assets[].name] | index(\"$LIST\") | if . == null then \"no\" else \"yes\" end")" = yes ]; then
    gh release download ota -R "$REPO" -p "$LIST" -D "$WORK" --clobber
  else
    : > "$WORK/$LIST"
  fi

  LIVE=$(open_pull_requests)
  drop_closed
  case "$EVENT" in
    push) publish_channel develop "$SHA" "$BRANCH: $(git -C "$ROOT" log -1 --format=%s)" ;;
    # Only a pull request that is open and from this repository gets a channel (a closed or fork one is not in $LIVE).
    pull_request_target) ! grep -qx "pr-$PR" <<<"$LIVE" || publish_channel "pr-$PR" "$SHA" "$TITLE" ;;
  esac
  [ "$changed" = 1 ] || { log "nothing to publish"; return 0; }

  local key="$WORK/key.pem"
  (umask 077; printf '%s\n' "$OTA_SIGNING_KEY" > "$key")
  key_matches_firmware "$key" || { log "OTA_SIGNING_KEY does not match mirror/ota/OtaKey.h: refusing to publish"; exit 1; }
  sign "$key" "$WORK/$LIST"
  rm -f "$key"
  gh release upload ota "$WORK/$LIST" "$WORK/$LIST.sig" -R "$REPO" --clobber
  log "published: $(cut -d' ' -f1 "$WORK/$LIST" | paste -sd' ' -)"
}

# Run when executed, not when sourced (mirror/scripts/ota/test.sh sources it).
if [ "${BASH_SOURCE[0]}" = "$0" ]; then main; fi
