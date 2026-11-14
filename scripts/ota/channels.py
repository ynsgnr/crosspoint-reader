#!/usr/bin/env python3
"""Maintains the OTA channel manifest (channels.json) that mirror/ota/OtaManifest.* reads on the device.

  channels.py add    --manifest FILE --id pr-19 --title TEXT --rev a16f221d --board x4pro --url URL \
                     --image FILE --built UNIX [--issued UNIX]
  channels.py remove --manifest FILE --id pr-19 [--issued UNIX]

The file is rewritten compactly and deterministically (sorted keys, ASCII only), so the bytes that get signed are exactly
the bytes that get served. The checks here mirror the device's policy (channelAllowed): a channel the device would
drop is refused here instead of being published. Signing is done by the workflow (openssl), not here.
"""
import argparse
import hashlib
import json
import os
import re
import sys
import time

RELEASE_BASE = "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/"
MAX_CHANNELS = 8
MAX_TITLE = 96
ID_RE = re.compile(r"^(develop|pr-[1-9][0-9]{0,5})$")
HEX_RE = re.compile(r"^[0-9a-f]{64}$")
REV_RE = re.compile(r"^[0-9a-f]{7,12}$")
FILE_RE = re.compile(r"^[A-Za-z0-9._-]+\.bin$")


def clean_title(text):
    """Printable ASCII/Unicode on one line, trimmed to what the device menu holds."""
    text = "".join(ch if ch.isprintable() else " " for ch in text)
    text = " ".join(text.split())
    return text[:MAX_TITLE] or "untitled"


def load(path):
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return {"v": 1, "issued": 0, "channels": []}
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    if doc.get("v") != 1 or not isinstance(doc.get("channels"), list):
        raise SystemExit("%s is not a version 1 manifest" % path)
    return doc


def dump(doc, path, issued):
    # `issued` never goes back: devices refuse a manifest older than one they accepted.
    doc["issued"] = max(int(issued), int(doc.get("issued", 0)) + 1)
    doc["channels"].sort(key=lambda c: (c["id"] != "develop", -int(c["id"][3:]) if c["id"] != "develop" else 0))
    if len(doc["channels"]) > MAX_CHANNELS:
        newest = sorted(doc["channels"], key=lambda c: c["built"], reverse=True)[:MAX_CHANNELS]
        doc["channels"] = [c for c in doc["channels"] if c in newest]
    text = json.dumps(doc, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    if len(text) > 12 * 1024:
        raise SystemExit("manifest would exceed the device's 16 KiB limit")
    with open(path, "w", encoding="ascii", newline="") as f:
        f.write(text)
    return doc


def image_info(path):
    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
            size += len(chunk)
    return h.hexdigest(), size


def cmd_add(a):
    if not ID_RE.match(a.id):
        raise SystemExit("bad channel id %r" % a.id)
    if not REV_RE.match(a.rev):
        raise SystemExit("bad revision %r" % a.rev)
    if not a.url.startswith(RELEASE_BASE) or not FILE_RE.match(a.url[len(RELEASE_BASE):]) or ".." in a.url:
        raise SystemExit("the image URL must be a plain file under %s" % RELEASE_BASE)
    sha, size = image_info(a.image)
    if not HEX_RE.match(sha) or size < 512 * 1024:
        raise SystemExit("%s does not look like a firmware image (%d bytes)" % (a.image, size))
    doc = load(a.manifest)
    doc["channels"] = [c for c in doc["channels"] if c["id"] != a.id]
    doc["channels"].append({"id": a.id, "title": clean_title(a.title), "rev": a.rev, "board": a.board, "url": a.url,
                            "sha256": sha, "size": size, "built": int(a.built)})
    dump(doc, a.manifest, a.issued or time.time())


def cmd_remove(a):
    doc = load(a.manifest)
    doc["channels"] = [c for c in doc["channels"] if c["id"] != a.id]
    dump(doc, a.manifest, a.issued or time.time())


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    add = sub.add_parser("add")
    add.add_argument("--manifest", required=True)
    add.add_argument("--id", required=True)
    add.add_argument("--title", required=True)
    add.add_argument("--rev", required=True)
    add.add_argument("--board", default="x4pro")
    add.add_argument("--url", required=True)
    add.add_argument("--image", required=True)
    add.add_argument("--built", required=True, type=int)
    add.add_argument("--issued", type=int)
    add.set_defaults(fn=cmd_add)
    rm = sub.add_parser("remove")
    rm.add_argument("--manifest", required=True)
    rm.add_argument("--id", required=True)
    rm.add_argument("--issued", type=int)
    rm.set_defaults(fn=cmd_remove)
    args = p.parse_args(argv)
    args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
