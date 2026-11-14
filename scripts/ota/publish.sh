#!/usr/bin/env bash
# Publishes the OTA channels (see .github/workflows/ota-publish.yml). Runs from the base branch only; it never executes
# anything from a pull request: the PR's firmware is downloaded, hashed and listed as opaque data.
#
# Environment: GH_TOKEN, REPO, KIND (develop | pr | sweep | reconcile), CHANNEL (develop | pr-N), SHA (the commit the
# image was built from), TITLE, OTA_SIGNING_KEY (PEM). The release "ota" holds channels.json, channels.json.sig and one
# image per channel.
#   develop / pr   publish that channel, after sweeping channels whose pull request is gone
#   sweep          only the sweep
#   reconcile      the sweep, then publish develop-retink and every open labelled PR that is missing or out of date
# Any failed GitHub call stops the run before anything is changed: a transient error must never shorten the manifest.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
MANIFEST="$WORK/channels.json"
BASE="https://github.com/${REPO}/releases/download/ota"
BOARD=x4pro
ASSET_SUFFIX=firmware-x4pro-mirror.bin   # artifact of the "mirror" workflow's x4pro-macro build
INTEGRATION_BRANCH=develop-retink
changed=0

log() { echo "ota: $*"; }

gh release view ota -R "$REPO" >/dev/null 2>&1 || gh release create ota -R "$REPO" --prerelease --latest=false \
  --title "OTA channels" --notes "Signed firmware builds for the Firmware channels screen. Written by CI; do not edit."

# Start from an empty manifest only when the release really has none. A manifest that exists but cannot be downloaded
# stops the run: continuing from an empty one would publish a list without every other channel.
if [ "$(gh release view ota -R "$REPO" --json assets --jq '[.assets[].name] | index("channels.json") | if . == null then "no" else "yes" end')" = yes ]; then
  gh release download ota -R "$REPO" -p channels.json -D "$WORK" --clobber
else
  : > "$MANIFEST"
fi

# Reads the manifest: "ids" lists the channel ids, "rev <id>" prints that channel's revision (empty when absent).
manifest() {
  python3 - "$MANIFEST" "$@" <<'PY'
import json, sys
path, what = sys.argv[1], sys.argv[2]
try:
    channels = json.load(open(path))["channels"]
except (OSError, ValueError, KeyError):
    channels = []
if what == "ids":
    print("\n".join(c["id"] for c in channels))
else:
    print(next((c["rev"] for c in channels if c["id"] == sys.argv[3]), ""))
PY
}

# 0 when pull request $1 is open, from this repository and labelled "ota". A call that fails for any reason other than
# "no such pull request" aborts the run (set -e) instead of reading as "closed".
pr_is_live() {
  local info
  if ! info="$(gh pr view "$1" -R "$REPO" --json state,labels,headRepository,headRepositoryOwner 2>"$WORK/err")"; then
    grep -qi "could not resolve" "$WORK/err" && return 1
    cat "$WORK/err" >&2
    exit 1
  fi
  python3 -c '
import json, sys
d, repo = json.load(sys.stdin), sys.argv[1].lower()
same = (d["headRepositoryOwner"]["login"] + "/" + d["headRepository"]["name"]).lower() == repo
sys.exit(0 if d["state"] == "OPEN" and same and any(l["name"] == "ota" for l in d["labels"]) else 1)' "$REPO" <<<"$info"
}

# A pull request channel lives only while its PR is open, from this repository and labelled "ota". Runs every time,
# so a missed close event is repaired by the next run.
sweep() {
  local id
  for id in $(manifest ids); do
    [[ "$id" == pr-* ]] || continue
    if ! pr_is_live "${id#pr-}"; then
      log "removing $id (closed, relabelled or gone)"
      python3 "$HERE/channels.py" remove --manifest "$MANIFEST" --id "$id"
      gh release delete-asset ota "$id-$ASSET_SUFFIX" -R "$REPO" -y 2>/dev/null || true
      changed=1
    fi
  done
}

# Adds or replaces channel $1 from the image the "mirror" workflow built for commit $2; $3 is the title.
publish_channel() {
  local channel="$1" sha="$2" title="$3" run image
  [[ "$channel" =~ ^(develop|pr-[1-9][0-9]{0,5})$ ]] || { log "bad channel id"; exit 1; }
  [[ "$sha" =~ ^[0-9a-f]{40}$ ]] || { log "bad commit"; exit 1; }
  # Wait for the build of exactly this commit; publish only a green one.
  run=""
  for _ in $(seq 1 60); do
    run="$(gh run list -R "$REPO" --workflow mirror.yml --commit "$sha" --json databaseId,status,conclusion --limit 10 \
      --jq 'map(select(.status=="completed")) | first | if . == null then "" else "\(.databaseId) \(.conclusion)" end')"
    [ -n "$run" ] && break
    sleep 30
  done
  if [ -z "$run" ]; then log "no finished build for $sha within 30 minutes (does the pull request change files the mirror workflow watches?): $channel not published"; return 0; fi
  if [ "${run#* }" != "success" ]; then log "the build for $sha ended '${run#* }': $channel not published"; return 0; fi
  mkdir -p "$WORK/dl"
  gh run download "${run% *}" -R "$REPO" -n "$ASSET_SUFFIX" -D "$WORK/dl"
  image="$WORK/$channel-$ASSET_SUFFIX"
  mv "$WORK/dl/firmware.bin" "$image"
  python3 "$HERE/channels.py" add --manifest "$MANIFEST" --id "$channel" --title "$title" --rev "${sha:0:8}" \
    --board "$BOARD" --url "$BASE/$channel-$ASSET_SUFFIX" --image "$image" --built "$(date +%s)"
  gh release upload ota "$image" -R "$REPO" --clobber   # the image first: the manifest never names a missing file
  changed=1
}

sweep

case "$KIND" in
  develop | pr) publish_channel "$CHANNEL" "$SHA" "$TITLE" ;;
  sweep) ;;
  reconcile)
    head="$(gh api "repos/$REPO/commits/$INTEGRATION_BRANCH" --jq '.sha + " " + (.commit.message | split("\n")[0])')"
    if [ "$(manifest rev develop)" != "${head:0:8}" ]; then publish_channel develop "${head%% *}" "$INTEGRATION_BRANCH: ${head#* }"; fi
    prs="$(gh pr list -R "$REPO" --base "$INTEGRATION_BRANCH" --label ota --state open \
      --json number,headRefOid,title,headRepository,headRepositoryOwner \
      --jq '.[] | select((.headRepositoryOwner.login + "/" + .headRepository.name | ascii_downcase) == ("'"$REPO"'" | ascii_downcase)) | "\(.number)\t\(.headRefOid)\t\(.title)"')"
    while IFS=$'\t' read -r n oid title; do
      [ -n "$n" ] || continue
      if [ "$(manifest rev "pr-$n")" != "${oid:0:8}" ]; then publish_channel "pr-$n" "$oid" "$title"; fi
    done <<<"$prs"
    ;;
  *) log "unknown KIND $KIND"; exit 1 ;;
esac

if [ "$changed" = 0 ]; then log "nothing to publish"; exit 0; fi

# The signing key must be the one compiled into the firmware (mirror/ota/OtaKey.h), or no device would accept the result.
KEY="$WORK/key.pem"
(umask 077; printf '%s\n' "$OTA_SIGNING_KEY" > "$KEY")
python3 - "$ROOT/mirror/ota/OtaKey.h" > "$WORK/firmware-pub.der" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
array = text[text.index("kOtaPublicKeyDer[]"):]
array = array[:array.index("};")]
sys.stdout.buffer.write(bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", array)))
PY
openssl ec -in "$KEY" -pubout -outform DER 2>/dev/null | cmp -s - "$WORK/firmware-pub.der" \
  || { log "OTA_SIGNING_KEY does not match mirror/ota/OtaKey.h: refusing to publish"; exit 1; }
openssl dgst -sha256 -sign "$KEY" -out "$WORK/channels.json.sig" "$MANIFEST"
openssl dgst -sha256 -verify <(openssl ec -in "$KEY" -pubout 2>/dev/null) -signature "$WORK/channels.json.sig" "$MANIFEST" >/dev/null
shred -u "$KEY" 2>/dev/null || rm -f "$KEY"

gh release upload ota "$MANIFEST" "$WORK/channels.json.sig" -R "$REPO" --clobber
log "published: $(manifest ids | paste -sd, - | sed 's/,/, /g')"
