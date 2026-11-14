import json
import os
import subprocess
import tempfile
import unittest

import channels

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "..", "..", "test", "ota", "channels_example.json")


def image(d, name, size=600_000, fill=b"\x5a"):
    path = os.path.join(d, name)
    with open(path, "wb") as f:
        f.write(fill * size)
    return path


def run(*argv):
    channels.main(list(argv))


def build_example(d):
    """The same manifest the C++ host test reads (mirror/test/ota/channels_example.json)."""
    m = os.path.join(d, "channels.json")
    base = channels.RELEASE_BASE
    run("add", "--manifest", m, "--id", "pr-19", "--title", "List template on the device", "--rev", "a16f221d",
        "--url", base + "pr-19-firmware-x4pro-mirror.bin", "--image", image(d, "a.bin", fill=b"\x01"),
        "--built", "1763100000", "--issued", "1763100000")
    run("add", "--manifest", m, "--id", "develop", "--title", "Integration branch é", "--rev", "97f15374",
        "--url", base + "develop-firmware-x4pro-mirror.bin", "--image", image(d, "b.bin", fill=b"\x02"),
        "--built", "1763000000", "--issued", "1763100001")
    return m


class ChannelsTest(unittest.TestCase):
    def test_example_fixture_is_what_the_script_writes(self):
        with tempfile.TemporaryDirectory() as d:
            text = open(build_example(d), encoding="ascii").read()
        with open(FIXTURE, encoding="ascii") as f:
            self.assertEqual(f.read(), text, "regenerate with: python3 mirror/scripts/ota/test_channels.py --write-fixture")

    def test_output_is_compact_ascii_and_ordered(self):
        with tempfile.TemporaryDirectory() as d:
            text = open(build_example(d), encoding="ascii").read()
        self.assertNotIn('", "', text)
        self.assertNotIn('": ', text)
        doc = json.loads(text)
        self.assertEqual([c["id"] for c in doc["channels"]], ["develop", "pr-19"])
        self.assertEqual(doc["issued"], 1763100001)
        self.assertEqual(doc["v"], 1)

    def test_issued_never_goes_back(self):
        with tempfile.TemporaryDirectory() as d:
            m = build_example(d)
            run("remove", "--manifest", m, "--id", "pr-19", "--issued", "5")
            self.assertEqual(json.load(open(m))["issued"], 1763100002)

    def test_replacing_a_channel_keeps_one_entry(self):
        with tempfile.TemporaryDirectory() as d:
            m = build_example(d)
            run("add", "--manifest", m, "--id", "pr-19", "--title", "t", "--rev", "bbbbbbbb",
                "--url", channels.RELEASE_BASE + "pr-19-firmware-x4pro-mirror.bin",
                "--image", image(d, "c.bin"), "--built", "1763200000")
            ids = [c["id"] for c in json.load(open(m))["channels"]]
            self.assertEqual(sorted(ids), ["develop", "pr-19"])

    def test_refuses_what_the_device_would_drop(self):
        with tempfile.TemporaryDirectory() as d:
            m = os.path.join(d, "channels.json")
            good = dict(manifest=m, title="t", rev="a16f221d", built="1")
            img = image(d, "i.bin")

            def add(**kw):
                a = ["add", "--manifest", m, "--id", kw.get("id", "pr-1"), "--title", "t", "--rev", kw.get("rev", "a16f221d"),
                     "--url", kw.get("url", channels.RELEASE_BASE + "x.bin"), "--image", kw.get("image", img), "--built", "1"]
                run(*a)

            for bad in (dict(id="main"), dict(id="pr-0"), dict(id="pr-1234567"), dict(rev="xyz"), dict(rev="A16F221D"),
                        dict(url="https://evil.example/x.bin"), dict(url=channels.RELEASE_BASE + "../x.bin"),
                        dict(url=channels.RELEASE_BASE + "x.exe"), dict(url=channels.RELEASE_BASE + "a/b.bin"),
                        dict(image=image(d, "small.bin", size=1000))):
                with self.assertRaises(SystemExit, msg=str(bad)):
                    add(**bad)
            self.assertFalse(os.path.exists(m))

    def test_at_most_eight_channels_newest_kept(self):
        with tempfile.TemporaryDirectory() as d:
            m = os.path.join(d, "channels.json")
            img = image(d, "i.bin")
            for n in range(1, 11):
                run("add", "--manifest", m, "--id", "pr-%d" % n, "--title", "t", "--rev", "a16f221d",
                    "--url", channels.RELEASE_BASE + "pr-%d.bin" % n, "--image", img, "--built", str(1000 + n))
            ids = [c["id"] for c in json.load(open(m))["channels"]]
            self.assertEqual(len(ids), channels.MAX_CHANNELS)
            self.assertNotIn("pr-1", ids)
            self.assertNotIn("pr-2", ids)

    def test_develop_is_never_trimmed_away(self):
        with tempfile.TemporaryDirectory() as d:
            m = os.path.join(d, "channels.json")
            img = image(d, "i.bin")
            run("add", "--manifest", m, "--id", "develop", "--title", "t", "--rev", "a16f221d",
                "--url", channels.RELEASE_BASE + "develop.bin", "--image", img, "--built", "1")  # the oldest
            for n in range(1, 11):
                run("add", "--manifest", m, "--id", "pr-%d" % n, "--title", "t", "--rev", "a16f221d",
                    "--url", channels.RELEASE_BASE + "pr-%d.bin" % n, "--image", img, "--built", str(1000 + n))
            ids = [c["id"] for c in json.load(open(m))["channels"]]
            self.assertEqual(len(ids), channels.MAX_CHANNELS)
            self.assertEqual(ids[0], "develop")
            self.assertIn("pr-10", ids)
            self.assertNotIn("pr-3", ids)

    def test_a_trailing_newline_is_not_an_id(self):
        for pattern, ok in ((channels.ID_RE, "pr-1"), (channels.HEX_RE, "a" * 64), (channels.REV_RE, "a16f221d"),
                            (channels.FILE_RE, "x.bin")):
            self.assertTrue(pattern.fullmatch(ok))
            self.assertFalse(pattern.fullmatch(ok + "\n"))

    def test_manifest_stays_within_the_device_limit(self):
        with tempfile.TemporaryDirectory() as d:
            m = os.path.join(d, "channels.json")
            img = image(d, "i.bin")
            for n in range(1, 9):
                run("add", "--manifest", m, "--id", "pr-%d" % n, "--title", "x" * channels.MAX_TITLE, "--rev", "a16f221d",
                    "--url", channels.RELEASE_BASE + "pr-%d-firmware-x4pro-mirror.bin" % n, "--image", img, "--built", "1")
            self.assertLess(os.path.getsize(m), channels.MAX_MANIFEST)

    def test_titles_are_one_clean_line(self):
        self.assertEqual(channels.clean_title("a\n\tb\x00c   d"), "a b c d")
        self.assertEqual(len(channels.clean_title("x" * 500)), channels.MAX_TITLE)
        self.assertEqual(channels.clean_title("\n"), "untitled")

    def test_openssl_signature_round_trip(self):
        """The workflow signs with `openssl dgst -sha256 -sign`; the device checks ECDSA P-256 / SHA-256 over the bytes."""
        with tempfile.TemporaryDirectory() as d:
            key, pub, sig = (os.path.join(d, n) for n in ("k.pem", "p.pem", "channels.json.sig"))
            subprocess.run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key], check=True,
                           capture_output=True)
            subprocess.run(["openssl", "ec", "-in", key, "-pubout", "-out", pub], check=True, capture_output=True)
            m = build_example(d)
            subprocess.run(["openssl", "dgst", "-sha256", "-sign", key, "-out", sig, m], check=True)
            ok = subprocess.run(["openssl", "dgst", "-sha256", "-verify", pub, "-signature", sig, m], capture_output=True)
            self.assertEqual(ok.returncode, 0)
            self.assertLessEqual(os.path.getsize(sig), 72)  # fits the device's 128 byte limit
            with open(m, "ab") as f:
                f.write(b" ")
            bad = subprocess.run(["openssl", "dgst", "-sha256", "-verify", pub, "-signature", sig, m], capture_output=True)
            self.assertNotEqual(bad.returncode, 0)


if __name__ == "__main__":
    import sys
    if "--write-fixture" in sys.argv:
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.dirname(FIXTURE), exist_ok=True)
            with open(FIXTURE, "w", encoding="ascii", newline="") as out:
                out.write(open(build_example(d), encoding="ascii").read())
        print("wrote", FIXTURE)
        sys.exit(0)
    unittest.main()
