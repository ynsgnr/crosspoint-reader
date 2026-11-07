#!/usr/bin/env bash
# Fails if a generated file or golden vector in mirror/ was edited by hand or is stale.
# The manifest is written by retink/scripts/sync-fork.sh and lists sha256 of every generated/golden file plus a hash of
# the spec they came from. This check needs no access to the Android repo; CI additionally runs the full
# `retink/scripts/sync-fork.sh --check` when it can read that repo.
set -euo pipefail
cd "$(dirname "$0")/.."
M=generated/MANIFEST.sha256
[ -f "$M" ] || { echo "missing $M"; exit 1; }
rc=0
while read -r hash path; do
  case "$path" in
    generated/*) f="$path" ;;
    golden/*) f="test/$path" ;;
    *) continue ;;
  esac
  [ "$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)" = "$hash" ] || { echo "MODIFIED or missing: $f"; rc=1; }
done < <(grep -v '^#' "$M")
# no extra generated/golden files either (count comparison)
n_listed=$(grep -v '^#' "$M" | wc -l); n_files=$(( $(find generated -type f ! -name MANIFEST.sha256 | wc -l) + $(find test/golden -type f | wc -l) ))
[ "$n_listed" = "$n_files" ] || { echo "manifest lists $n_listed files, tree has $n_files"; rc=1; }
[ $rc = 0 ] && echo "generated files match the manifest ($(head -1 "$M"))"
exit $rc
