import json
import os
import subprocess
import tempfile
import unittest

import manifest as mf

BASE = "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/"
SHA = "a" * 64
HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "..", "..", "mirror", "test", "ota", "channels.sample.json")


def entry(m, id, now=1763100000, title="T", rev="a16f221d"):
    return mf.add(m, id=id, title=title, rev=rev, board="x4pro", url=BASE + id + ".bin", sha256=SHA, size=3000000,
                  built=now, now=now)


class ManifestTest(unittest.TestCase):
    def test_add_replace_and_issued_goes_forward(self):
        m = mf.empty()
        entry(m, "pr-19")
        self.assertEqual(m["issued"], 1763100000)
        entry(m, "pr-19", now=1763100000, rev="97f15374")  # same second: still strictly newer
        self.assertEqual(m["issued"], 1763100001)
        self.assertEqual([c["rev"] for c in m["channels"]], ["97f15374"])

    def test_order_limit_and_remove(self):
        m = mf.empty()
        for i in (3, 12, 7):
            entry(m, "pr-%d" % i)
        entry(m, "develop")
        self.assertEqual([c["id"] for c in m["channels"]], ["develop", "pr-12", "pr-7", "pr-3"])
        for i in range(20, 30):
            entry(m, "pr-%d" % i)
        self.assertEqual(len(m["channels"]), mf.MAX_CHANNELS)
        self.assertEqual(m["channels"][0]["id"], "develop")
        self.assertEqual(m["channels"][-1]["id"], "pr-23")  # the oldest pull requests were dropped
        before = m["issued"]
        mf.remove(m, "pr-23", 1)
        self.assertNotIn("pr-23", [c["id"] for c in m["channels"]])
        self.assertEqual(m["issued"], before + 1)
        mf.remove(m, "pr-999", 1)  # unknown: nothing changes
        self.assertEqual(m["issued"], before + 1)

    def test_rejects_what_the_device_would_refuse(self):
        m = mf.empty()
        for bad in ("main", "pr-0", "pr-1234567", "pr-1a", "../x"):
            with self.assertRaises(SystemExit):
                entry(m, bad)
        with self.assertRaises(SystemExit):
            mf.add(m, id="pr-1", title="t", rev="zzzz", board="x4pro", url=BASE + "a.bin", sha256=SHA, size=1, built=1, now=1)

    def test_titles_are_one_clean_line_and_output_is_ascii_and_stable(self):
        self.assertEqual(mf.clean_title("a\n\tb \x00 c"), "a b c")
        self.assertEqual(len(mf.clean_title("x" * 500)), 96)
        self.assertEqual(mf.clean_title("   "), "untitled")
        m = mf.empty()
        entry(m, "pr-1", title="Café \U0001F600")
        text = mf.dump(m)
        text.encode("ascii")  # \u escapes only
        self.assertEqual(json.loads(text)["channels"][0]["title"], "Café \U0001F600")
        self.assertEqual(text, mf.dump(json.loads(text)))  # the same bytes every time

    def test_size_limit(self):
        m = mf.empty()
        m["channels"] = [{"id": "pr-%d" % i, "title": "x" * 96, "rev": "a" * 8, "board": "x4pro", "url": BASE + "x" * 5000 + ".bin",
                          "sha256": SHA, "size": 1, "built": 1} for i in range(1, 4)]
        with self.assertRaises(SystemExit):
            mf.dump(m)

    def test_load_missing_or_empty(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(mf.load(os.path.join(d, "none.json")), mf.empty())
            p = os.path.join(d, "e.json")
            open(p, "w").close()
            self.assertEqual(mf.load(p), mf.empty())
            open(p, "w").write('{"v":2,"channels":[]}')
            with self.assertRaises(SystemExit):
                mf.load(p)

    def test_cli_round_trip_and_asset_names(self):
        with tempfile.TemporaryDirectory() as d:
            m, img = os.path.join(d, "channels.json"), os.path.join(d, "x.bin")
            open(img, "wb").write(b"firmware")
            args = ["add", "--manifest", m, "--id", "pr-5", "--title", "five", "--rev", "a16f221d", "--asset", "pr-5-a16f221d.bin",
                    "--base", BASE, "--image", img]
            self.assertEqual(mf.main(args), 0)
            got = json.load(open(m))["channels"][0]
            self.assertEqual(got["sha256"], mf.sha256_file(img))
            self.assertEqual(got["size"], 8)
            self.assertEqual(mf.asset_name(got["url"]), "pr-5-a16f221d.bin")

    def test_fixture_the_firmware_tests_parse_is_what_this_script_writes(self):
        m = mf.empty()
        mf.add(m, id="pr-19", title="List template on the device", rev="a16f221d", board="x4pro", url=BASE + "pr-19-a16f221d.bin",
               sha256="0123456789abcdef" * 4, size=3012345, built=1763100000, now=1763100000)
        mf.add(m, id="develop", title="develop branch", rev="97f15374", board="x4pro", url=BASE + "develop-97f15374.bin",
               sha256="fedcba9876543210" * 4, size=3000000, built=1763099000, now=1763100000)
        expected = mf.dump(m)
        if os.environ.get("UPDATE_FIXTURE"):
            open(FIXTURE, "w").write(expected)
        self.assertEqual(open(FIXTURE).read(), expected, "run: UPDATE_FIXTURE=1 python3 scripts/ota/test_manifest.py")

    def test_signing_recipe_and_key_check(self):
        if subprocess.run(["which", "openssl"], stdout=subprocess.DEVNULL).returncode:
            self.skipTest("no openssl")
        with tempfile.TemporaryDirectory() as d:
            pem, other = os.path.join(d, "k.pem"), os.path.join(d, "o.pem")
            for p in (pem, other):
                subprocess.run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", p], check=True,
                               stderr=subprocess.DEVNULL)
            der = subprocess.run(["openssl", "ec", "-in", pem, "-pubout", "-outform", "DER"], check=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL).stdout
            header = os.path.join(d, "OtaKey.h")
            with open(header, "w") as f:
                f.write("inline constexpr uint8_t kOtaPublicKeyDer[] = {\n    " + ", ".join("0x%02x" % b for b in der) + ",\n};\n")
            self.assertTrue(mf.key_matches(pem, header))
            self.assertFalse(mf.key_matches(other, header))
            # The signature the workflow makes verifies against the public key, and not after the bytes change.
            doc, sig, pub = (os.path.join(d, n) for n in ("channels.json", "channels.json.sig", "pub.pem"))
            open(doc, "w").write('{"v":1}')
            subprocess.run(["openssl", "dgst", "-sha256", "-sign", pem, "-out", sig, doc], check=True)
            subprocess.run(["openssl", "ec", "-in", pem, "-pubout", "-out", pub], check=True, stderr=subprocess.DEVNULL)
            ok = lambda: subprocess.run(["openssl", "dgst", "-sha256", "-verify", pub, "-signature", sig, doc],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
            self.assertTrue(ok())
            open(doc, "w").write('{"v":2}')
            self.assertFalse(ok())


if __name__ == "__main__":
    unittest.main()
