#!/usr/bin/env bash
# Publishes the OTA channels (see .github/workflows/ota-publish.yml). Runs from the base branch only; it never executes
# anything from a pull request: the PR's firmware is downloaded, hashed and listed as opaque data.
#
# Environment: GH_TOKEN, REPO, KIND (develop | pr | sweep), CHANNEL (develop | pr-N), SHA (the commit the image was built
# from), TITLE, OTA_SIGNING_KEY (PEM). The release "ota" holds channels.json, channels.json.sig and one image per channel.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
MANIFEST="$WORK/channels.json"
BASE="https://github.com/${REPO}/releases/download/ota"
BOARD=x4pro
ASSET_SUFFIX=firmware-x4pro-mirror.bin   # artifact of the "mirror" workflow's x4pro-macro build

log() { echo "ota: $*"; }

gh release view ota -R "$REPO" >/dev/null 2>&1 || gh release create ota -R "$REPO" --prerelease --latest=false \
  --title "OTA channels" --notes "Signed firmware builds for the Firmware channels screen. Written by CI; do not edit."
gh release download ota -R "$REPO" -p channels.json -D "$WORK" 2>/dev/null || : > "$MANIFEST"

# Self-healing sweep: a pull request channel lives only while its PR is open, from this repository and labelled "ota".
# Runs on every invocation, so a missed close event is repaired by the next run.
changed=0
for id in $(python3 - "$MANIFEST" <<'PY'
import json, sys
try:
    print("\n".join(c["id"] for c in json.load(open(sys.argv[1]))["channels"] if c["id"].startswith("pr-")))
except (OSError, ValueError, KeyError):
    pass
PY
); do
  n="${id#pr-}"
  live="$(gh pr view "$n" -R "$REPO" --json state,labels,headRepository,headRepositoryOwner \
    --jq 'select(.state=="OPEN" and ((.headRepositoryOwner.login + "/" + .headRepository.name) | ascii_downcase) == ("'"$REPO"'" | ascii_downcase) and ([.labels[].name] | index("ota"))) | "yes"' 2>/dev/null || true)"
  if [ "$live" != "yes" ]; then
    log "removing $id (closed, relabelled or gone)"
    python3 "$HERE/channels.py" remove --manifest "$MANIFEST" --id "$id"
    gh release delete-asset ota "$id-$ASSET_SUFFIX" -R "$REPO" -y 2>/dev/null || true
    changed=1
  fi
done

if [ "$KIND" != "sweep" ]; then
  [[ "$CHANNEL" =~ ^(develop|pr-[1-9][0-9]{0,5})$ ]] || { log "bad channel id"; exit 1; }
  [[ "$SHA" =~ ^[0-9a-f]{40}$ ]] || { log "bad commit"; exit 1; }
  # The image comes from the "mirror" workflow's run for exactly this commit. Wait for it; publish only a green one.
  run=""
  for _ in $(seq 1 60); do
    run="$(gh run list -R "$REPO" --workflow mirror.yml --commit "$SHA" --json databaseId,status,conclusion --limit 10 \
      --jq 'map(select(.status=="completed")) | first | if . == null then "" else "\(.databaseId) \(.conclusion)" end')"
    [ -n "$run" ] && break
    sleep 30
  done
  [ -n "$run" ] || { log "no finished build for $SHA within 30 minutes: nothing published"; exit 0; }
  if [ "${run#* }" != "success" ]; then log "the build for $SHA ended '${run#* }': nothing published"; exit 0; fi
  mkdir -p "$WORK/dl"
  gh run download "${run% *}" -R "$REPO" -n "$ASSET_SUFFIX" -D "$WORK/dl"
  image="$WORK/$CHANNEL-$ASSET_SUFFIX"
  mv "$WORK/dl/firmware.bin" "$image"
  python3 "$HERE/channels.py" add --manifest "$MANIFEST" --id "$CHANNEL" --title "$TITLE" --rev "${SHA:0:8}" \
    --board "$BOARD" --url "$BASE/$CHANNEL-$ASSET_SUFFIX" --image "$image" --built "$(date +%s)"
  gh release upload ota "$image" -R "$REPO" --clobber   # the image first: the manifest never names a missing file
  changed=1
fi

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
log "published: $(python3 -c 'import json,sys; print(", ".join(c["id"] for c in json.load(open(sys.argv[1]))["channels"]) or "(no channels)")' "$MANIFEST")"
