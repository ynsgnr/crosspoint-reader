#!/usr/bin/env python3
"""The OTA channel manifest (channels.json) that .github/workflows/mirror.yml publishes and the firmware reads.

Format (mirror/ota/OtaManifest.h is the reader; the device checks the signature before it parses a byte):
  {"v":1,"issued":<unix>,"channels":[{"id":"pr-19","title":"..","rev":"a16f221d","board":"x4pro","url":"https://..","sha256":"<64 hex>","size":N,"built":<unix>}]}

  manifest.py add    --manifest F --id pr-19 --title T --rev R --board B --asset NAME --base URL --image FILE
  manifest.py remove --manifest F --id pr-19
  manifest.py assets --manifest F --id pr-19        # asset names the channel points at (before add/remove), one per line
  manifest.py check-key --key PEM --header mirror/ota/OtaKey.h   # the signing key is the one the firmware trusts
Standard library only; signing is `openssl dgst -sha256 -sign` in the workflow.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

BOARD_DEFAULT = "x4pro"
MAX_CHANNELS = 8  # the device keeps at most this many (kMaxChannels)
MAX_BYTES = 12000  # well under the device's 16 KiB document limit
CHANNEL_ID = re.compile(r"^(develop|pr-[1-9][0-9]{0,5})$")
FIELDS = ("id", "title", "rev", "board", "url", "sha256", "size", "built")


def empty():
    return {"v": 1, "issued": 0, "channels": []}


def load(path):
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return empty()
    with open(path, encoding="utf-8") as f:
        m = json.load(f)
    if m.get("v") != 1 or not isinstance(m.get("channels"), list):
        raise SystemExit("%s is not a v1 manifest" % path)
    return m


def clean_title(title):
    """One line of printable text, at most 96 characters (the device's menu has no room for more)."""
    t = "".join(c if c.isprintable() else " " for c in title).strip()
    t = re.sub(r"\s+", " ", t)
    return t[:96] or "untitled"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def tidy(m):
    """develop first, then pull requests newest first; at most MAX_CHANNELS (the oldest pull requests go first)."""
    def key(c):
        return (0, 0) if c["id"] == "develop" else (1, -int(c["id"][3:]))
    m["channels"].sort(key=key)
    del m["channels"][MAX_CHANNELS:]


def add(m, *, id, title, rev, board, url, sha256, size, built, now):
    if not CHANNEL_ID.match(id):
        raise SystemExit("not a channel id: %r" % id)
    if not re.fullmatch(r"[0-9a-f]{64}", sha256) or not re.fullmatch(r"[0-9a-f]{7,12}", rev):
        raise SystemExit("bad sha256 or rev")
    m["channels"] = [c for c in m["channels"] if c["id"] != id]
    m["channels"].append({"id": id, "title": clean_title(title), "rev": rev, "board": board, "url": url,
                          "sha256": sha256, "size": size, "built": built})
    m["issued"] = max(now, m.get("issued", 0) + 1)  # strictly forward: the device refuses an older manifest
    tidy(m)
    return m


def remove(m, id, now):
    n = len(m["channels"])
    m["channels"] = [c for c in m["channels"] if c["id"] != id]
    if len(m["channels"]) != n:
        m["issued"] = max(now, m.get("issued", 0) + 1)
    return m


def dump(m):
    text = json.dumps(m, separators=(",", ":"), sort_keys=True, ensure_ascii=True)
    if len(text) > MAX_BYTES:
        raise SystemExit("manifest is %d bytes, over the %d limit" % (len(text), MAX_BYTES))
    return text


def asset_name(url):
    return url.rsplit("/", 1)[-1]


def header_key(path):
    """The public key bytes in OtaKey.h (kOtaPublicKeyDer)."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    body = re.search(r"kOtaPublicKeyDer\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not body:
        raise SystemExit("no kOtaPublicKeyDer in %s" % path)
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body.group(1)))


def key_matches(pem, header):
    der = subprocess.run(["openssl", "ec", "-in", pem, "-pubout", "-outform", "DER"], check=True,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout
    return der == header_key(header)


def main(argv=None):
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("add")
    for n in ("manifest", "id", "title", "rev", "asset", "base", "image"):
        a.add_argument("--" + n, required=True)
    a.add_argument("--board", default=BOARD_DEFAULT)
    r = sub.add_parser("remove")
    r.add_argument("--manifest", required=True)
    r.add_argument("--id", required=True)
    s = sub.add_parser("assets")
    s.add_argument("--manifest", required=True)
    s.add_argument("--id", required=True)
    k = sub.add_parser("check-key")
    k.add_argument("--key", required=True)
    k.add_argument("--header", required=True)
    args = p.parse_args(argv)

    now = int(time.time())
    if args.cmd == "check-key":
        if not key_matches(args.key, args.header):
            raise SystemExit("OTA_SIGNING_KEY is not the key whose public half is in %s: devices would refuse every build" % args.header)
        return 0
    m = load(args.manifest)
    if args.cmd == "assets":
        for c in m["channels"]:
            if c["id"] == args.id:
                print(asset_name(c["url"]))
        return 0
    if args.cmd == "add":
        add(m, id=args.id, title=args.title, rev=args.rev, board=args.board, url=args.base + args.asset,
            sha256=sha256_file(args.image), size=os.path.getsize(args.image), built=now, now=now)
    else:
        remove(m, args.id, now)
    with open(args.manifest, "w", encoding="utf-8") as f:
        f.write(dump(m))
    return 0


if __name__ == "__main__":
    sys.exit(main())
