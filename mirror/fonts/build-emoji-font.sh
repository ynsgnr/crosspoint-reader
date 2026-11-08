#!/usr/bin/env bash
# Rebuilds mirror/fonts/LibroEmoji: Libron with Noto Emoji as the fallback, as CrossPoint .cpfont files.
# Needs python3 with freetype-py and fonttools. Sources are pinned, so the output only changes when they do.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/mirror/fonts/LibroEmoji"
LIBRON_VERSION=v0.32
NOTO_COMMIT=b979dba422e445492b0eb9951ac52ee0b4d648c3  # google/fonts, ofl/notoemoji
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# LIBRON_ZIP=path uses an already downloaded release zip (e.g. from `gh release download`).
if [ -n "${LIBRON_ZIP:-}" ]; then cp "$LIBRON_ZIP" "$TMP/Libron.zip"; else
  curl -fsSL --retry 5 --retry-all-errors -o "$TMP/Libron.zip" "https://github.com/nicoverbruggen/libron/releases/download/$LIBRON_VERSION/Libron.zip"
fi
curl -fsSL --retry 5 --retry-all-errors -o "$TMP/LICENSE-Libron.txt" "https://raw.githubusercontent.com/nicoverbruggen/libron/$LIBRON_VERSION/LICENSE"
curl -fsSL --retry 5 --retry-all-errors -o "$TMP/NotoEmoji-VF.ttf" "https://raw.githubusercontent.com/google/fonts/$NOTO_COMMIT/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf"
curl -fsSL --retry 5 --retry-all-errors -o "$TMP/LICENSE-NotoEmoji.txt" "https://raw.githubusercontent.com/google/fonts/$NOTO_COMMIT/ofl/notoemoji/OFL.txt"
python3 -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$TMP/Libron.zip" "$TMP/libron"
# The converter takes static fonts: pin the variable Noto Emoji at its regular weight.
python3 -m fontTools.varLib.instancer "$TMP/NotoEmoji-VF.ttf" wght=400 -q -o "$TMP/NotoEmoji-Regular.ttf"

# Libron's own glyphs come first; Noto Emoji fills only what Libron lacks. Sizes 8-12 are what the UI fallback
# (and so the Mirror screen) uses, 12-18 the reading sizes. ipa-chars brings Libron's "ə", one of the scripts the
# firmware checks before it uses a reading font as the UI fallback (kFallbackProbes in src/SdCardFontSystem.cpp).
L="$TMP/libron"
E="$TMP/NotoEmoji-Regular.ttf"
rm -rf "$OUT" && mkdir -p "$OUT"
python3 "$ROOT/lib/EpdFont/scripts/fontconvert_sdcard.py" \
  --regular "$L/Libron-Regular.ttf" --bold "$L/Libron-Bold.ttf" \
  --italic "$L/Libron-Italic.ttf" --bolditalic "$L/Libron-BoldItalic.ttf" \
  --fallback-regular "$E" --fallback-bold "$E" --fallback-italic "$E" --fallback-bolditalic "$E" \
  --intervals 'latin-ext,ipa-chars,punctuation,(0x2190-0x21FF),(0x2300-0x23FF),(0x2600-0x27BF),(0x2B00-0x2BFF),(0xFE0F-0xFE0F),(0x1F000-0x1FAFF)' \
  --sizes 8,10,12,14,16,18 --name LibroEmoji --output-dir "$OUT"
cp "$TMP/LICENSE-Libron.txt" "$TMP/LICENSE-NotoEmoji.txt" "$OUT/"
cat > "$OUT/FONTLOG.txt" <<'LOG'
LibroEmoji
==========

LibroEmoji is a Modified Version of two fonts, both licensed under the SIL Open Font License 1.1:

* Libron by Nico Verbruggen, https://github.com/nicoverbruggen/libron (version v0.32)
  Newsreader (c) 2020 The Newsreader Project Authors (http://github.com/productiontype/Newsreader)
  Readerly (c) 2026 Nico Verbruggen (https://github.com/nicoverbruggen/readerly)
  Libron (c) 2026 Nico Verbruggen (https://github.com/nicoverbruggen/libron)
  with Reserved Font Name Libron.
  License: LICENSE-Libron.txt

* Noto Emoji (monochrome), Copyright 2013, 2022 Google Inc.
  https://github.com/google/fonts/tree/main/ofl/notoemoji
  License: LICENSE-NotoEmoji.txt

Changes: Libron's four styles were combined with Noto Emoji (variable font pinned at weight 400) as a fallback for
the characters Libron does not have, and converted to CrossPoint's bitmap .cpfont format at 8-18 pt. Libron's own
glyphs are unchanged. Because it is modified, it does not use the Reserved Font Name "Libron".

It is not an official Libron release and is not endorsed by its authors. All credit for the letterforms goes to
Nico Verbruggen and the Newsreader Project Authors, and for the emoji to Google.

Rebuild: mirror/fonts/build-emoji-font.sh in https://github.com/ynsgnr/crosspoint-reader
LOG
echo "wrote $OUT"
