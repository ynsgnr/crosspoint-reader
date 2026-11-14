#include "tests/ota_tests.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "ota/BootGuard.h"
#include "ota/MiniJson.h"
#include "ota/OtaManifest.h"

using namespace mirror::ota;

namespace {

int g_checks = 0, g_fail = 0;
#define OCHECK(c)                                              \
  do {                                                         \
    ++g_checks;                                                \
    if (!(c)) {                                                \
      ++g_fail;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                          \
  } while (0)

// Accepts exactly the signature bytes "GOOD" and records that it was asked.
class FakeVerifier : public SignatureVerifier {
 public:
  mutable int calls = 0;
  bool verify(const uint8_t*, size_t, const uint8_t* sig, size_t sigLen) const override {
    ++calls;
    return sigLen == 4 && std::string(reinterpret_cast<const char*>(sig), 4) == "GOOD";
  }
};
const uint8_t kGood[] = {'G', 'O', 'O', 'D'};
const uint8_t kBad[] = {'B', 'A', 'D', '!'};

const char* const kPrefix = "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/";
const std::string kSha(64, 'a');

Policy policy(uint32_t lastIssued = 0) {
  Policy p;
  p.board = "x4pro";
  p.urlPrefix = kPrefix;
  p.maxImageBytes = 6 * 1024 * 1024;
  p.lastIssued = lastIssued;
  return p;
}

std::string channel(const std::string& id, const std::string& file, const std::string& board = "x4pro",
                    const std::string& sha = kSha, long size = 3000000, const std::string& url = "") {
  return "{\"id\":\"" + id + "\",\"title\":\"Title of " + id + "\",\"rev\":\"a16f221d\",\"board\":\"" + board +
         "\",\"url\":\"" + (url.empty() ? std::string(kPrefix) + file : url) + "\",\"sha256\":\"" + sha +
         "\",\"size\":" + std::to_string(size) + ",\"built\":1763100000}";
}

std::string manifest(const std::string& channels, long issued = 1763100000, int v = 1) {
  return "{\"v\":" + std::to_string(v) + ",\"issued\":" + std::to_string(issued) + ",\"channels\":[" + channels + "]}";
}

Trust open(const std::string& text, Manifest& m, const Policy& p = policy(), bool good = true) {
  static const FakeVerifier v;
  return openManifest(text, good ? kGood : kBad, 4, v, p, m);
}

void testJson() {
  json::Value v;
  OCHECK(json::parse(" {\"a\": [1, -2, \"x\\u00e9\\ud83d\\ude00\\n\", true, false, null], \"b\": {}} ", v));
  OCHECK(v.get("a") && v.get("a")->items.size() == 6 && v.get("a")->items[1].i == -2);
  OCHECK(v.get("a")->items[2].s == "x\xC3\xA9\xF0\x9F\x98\x80\n");  // e-acute and an emoji, as UTF-8
  OCHECK(v.get("b") && v.get("b")->type == json::Value::Type::Object && !v.get("missing"));
  const char* bad[] = {"",
                       "{",
                       "{\"a\":1,}",
                       "[1,]",
                       "{\"a\":1} x",
                       "{\"a\":1,\"a\":2}",
                       "{\"a\":1.5}",
                       "{\"a\":01}",
                       "{\"a\":1e3}",
                       "\"\\x\"",
                       "\"\\ud800\"",
                       "\"\\udc00\"",
                       "\"abc",
                       "[\"a\tb\"]",
                       "nul",
                       "{a:1}",
                       "{\"a\":1234567890123456}",
                       "-",
                       "{\"a\" 1}"};
  for (const char* t : bad) OCHECK(!json::parse(t, v));
  std::string deep(json::kMaxDepth + 2, '[');
  deep += std::string(json::kMaxDepth + 2, ']');
  OCHECK(!json::parse(deep, v));
  OCHECK(!json::parse("\"" + std::string(json::kMaxString + 1, 'x') + "\"", v));
  OCHECK(!json::parse("[" + std::string(json::kMaxDocument, ' ') + "1]", v));
  std::string many = "[";
  for (size_t i = 0; i < json::kMaxNodes + 1; ++i) many += "1,";
  OCHECK(!json::parse(many + "1]", v));
  OCHECK(v.type == json::Value::Type::Null);  // a failed parse leaves nothing half built
}

void testChannelIds() {
  OCHECK(isChannelId("develop") && isChannelId("pr-1") && isChannelId("pr-123456"));
  const char* bad[] = {"",      "main", "pr-",   "pr-0",     "pr-012", "pr-1234567",
                       "pr-1a", "PR-1", "pr--1", "develop2", "../pr-1"};
  for (const char* id : bad) OCHECK(!isChannelId(id));
}

void testSignatureComesFirst() {
  Manifest m;
  const FakeVerifier v;
  // Garbage that is not JSON: with a bad signature it must be refused as unsigned, never parsed.
  OCHECK(openManifest("not json at all", kBad, 4, v, policy(), m) == Trust::BadSignature && v.calls == 1);
  OCHECK(openManifest("not json at all", kGood, 4, v, policy(), m) == Trust::BadDocument);
  OCHECK(openManifest(manifest(channel("pr-1", "a.bin")), nullptr, 0, v, policy(), m) == Trust::BadSignature);
  OCHECK(openManifest("", kGood, 4, v, policy(), m) == Trust::BadSignature);
  OCHECK(open(manifest(channel("pr-1", "a.bin")), m, policy(), false) == Trust::BadSignature && m.channels.empty());
  OCHECK(open(manifest(channel("pr-1", "a.bin")), m) == Trust::Ok && m.channels.size() == 1);
}

void testDocumentRules() {
  Manifest m;
  OCHECK(open(manifest("", 1763100000, 2), m) == Trust::BadDocument);  // another format version
  OCHECK(open(manifest("", 0), m) == Trust::BadDocument);
  OCHECK(open("{\"v\":1,\"issued\":5}", m) == Trust::BadDocument);
  OCHECK(open("[]", m) == Trust::BadDocument);
  // A channel with a missing field spoils the whole document: it is not a manifest we wrote.
  OCHECK(open(manifest("{\"id\":\"pr-1\"}"), m) == Trust::BadDocument && m.channels.empty());
  // Replay: an older manifest than the newest one accepted before.
  OCHECK(open(manifest(channel("pr-1", "a.bin"), 1763100000), m, policy(1763100001)) == Trust::Stale);
  OCHECK(open(manifest(channel("pr-1", "a.bin"), 1763100000), m, policy(1763100000)) == Trust::Ok);
}

void testPolicyDropsWhatItDoesNotAccept() {
  Manifest m;
  const std::string good = channel("pr-2", "pr-2-firmware.bin");
  const std::string others =
      channel("pr-3", "x.bin", "x4") + "," +                                                      // another board
      channel("pr-4", "x.bin", "x4pro", kSha, 3000000, "https://evil.example/ota/x.bin") + "," +  // another host
      channel("pr-5", "x.bin", "x4pro", kSha, 3000000, std::string(kPrefix) + "../x.bin") + "," +
      channel("pr-6", "x.bin?token=1") + "," + channel("pr-7", "sub/x.bin") + "," + channel("pr-8", "x.exe") + "," +
      channel("pr-9", "x.bin", "x4pro", std::string(63, 'a')) + "," +    // short hash
      channel("pr-10", "x.bin", "x4pro", std::string(64, 'A')) + "," +   // not lower case
      channel("pr-11", "x.bin", "x4pro", kSha, 1000) + "," +             // too small
      channel("pr-12", "x.bin", "x4pro", kSha, 7 * 1024 * 1024) + "," +  // too large
      channel("main", "x.bin") + "," + channel("pr-13", ".bin") + "," +
      channel("pr-14", "http.bin", "x4pro", kSha, 3000000,
              "http://github.com/ynsgnr/crosspoint-reader/releases/download/ota/h.bin");
  OCHECK(open(manifest(others + "," + good), m) == Trust::Ok);
  OCHECK(m.channels.size() == 1 && m.channels[0].id == "pr-2");
  OCHECK(m.channels[0].title == "Title of pr-2" && m.channels[0].size == 3000000 && m.channels[0].board == "x4pro");
  // An empty prefix would accept anything: it accepts nothing instead.
  Policy open_ = policy();
  open_.urlPrefix = "";
  OCHECK(open(manifest(good), m, open_) == Trust::Ok && m.channels.empty());
}

void testOrderDuplicatesAndLimit() {
  Manifest m;
  OCHECK(open(manifest(channel("pr-2", "a.bin") + "," + channel("pr-10", "b.bin") + "," + channel("develop", "d.bin") +
                       "," + channel("pr-2", "dup.bin") + "," + channel("pr-9", "c.bin")),
              m) == Trust::Ok);
  OCHECK(m.channels.size() == 4);
  OCHECK(m.channels[0].id == "develop" && m.channels[1].id == "pr-10" && m.channels[2].id == "pr-9" &&
         m.channels[3].id == "pr-2");
  OCHECK(m.channels[3].url == std::string(kPrefix) + "a.bin");  // the first of two duplicates wins
  std::string many;
  for (int i = 1; i <= 12; ++i)
    many += (i > 1 ? "," : "") + channel("pr-" + std::to_string(i), "f" + std::to_string(i) + ".bin");
  OCHECK(open(manifest(many), m) == Trust::Ok && m.channels.size() == kMaxChannels && m.channels[0].id == "pr-12");
}

void testRunningBuild() {
  Channel c;
  c.rev = "a16f221d";
  OCHECK(!differsFromRunning(c, "a16f221d"));
  OCHECK(differsFromRunning(c, "97f15374"));
  OCHECK(differsFromRunning(c, "a16f221d+"));  // uncommitted changes: never the same build
  OCHECK(differsFromRunning(c, ""));
  OCHECK(std::string(trustName(Trust::Stale)) == "STALE" && std::string(trustName(Trust::Ok)) == "OK");
}

// What mirror/scripts/ota/channels.py writes (the fixture is checked against the script by
// mirror/scripts/ota/test_channels.py): the device side reads it, so the two ends cannot drift apart.
void testManifestWrittenByTheScript() {
  std::ifstream f(std::string(MIRROR_GOLDEN_DIR) + "/../ota/channels_example.json");
  std::stringstream ss;
  ss << f.rdbuf();
  Manifest m;
  OCHECK(!ss.str().empty() && open(ss.str(), m) == Trust::Ok);
  OCHECK(m.channels.size() == 2 && m.issued == 1763100001);
  OCHECK(m.channels[0].id == "develop" && m.channels[0].title == "Integration branch \xC3\xA9" &&
         m.channels[0].rev == "97f15374");
  OCHECK(m.channels[1].id == "pr-19" && m.channels[1].size == 600000 && m.channels[1].built == 1763100000);
  OCHECK(m.channels[1].url == std::string(kPrefix) + "pr-19-firmware-x4pro-mirror.bin" &&
         m.channels[1].sha256.size() == 64);
}

// The file mirror/scripts/ota/channels.py writes (checked in by its own test) is what the device must understand.
void testManifestFromTheCiScript() {
  std::ifstream f(std::string(MIRROR_OTA_FIXTURE_DIR) + "/channels_example.json");
  OCHECK(static_cast<bool>(f));
  std::stringstream ss;
  ss << f.rdbuf();
  Manifest m;
  OCHECK(open(ss.str(), m) == Trust::Ok);
  OCHECK(m.issued == 1763100001 && m.channels.size() == 2);
  OCHECK(m.channels[0].id == "develop" && m.channels[0].rev == "97f15374" && m.channels[0].size == 600000);
  OCHECK(m.channels[0].title == "Integration branch \xC3\xA9");  // the \u00e9 escape, as UTF-8
  OCHECK(m.channels[1].id == "pr-19" && m.channels[1].title == "List template on the device" &&
         m.channels[1].url == std::string(kPrefix) + "pr-19-firmware-x4pro-mirror.bin" &&
         m.channels[1].sha256.size() == 64);
  OCHECK(open(ss.str(), m, policy(1763100002)) == Trust::Stale);  // a newer manifest was already accepted
}

void testBootGuardTrialAndRollback() {
  BootGuard g;
  OCHECK(onBootGuard(g, "a16f221d") == BootAction::Nothing);  // no trial: nothing happens
  armGuard(g, 1, "a16f221d");
  OCHECK(g.pending && g.tries == 0 && g.prevSlot == 1 && !g.rolledBack);
  // A build that never confirms gets kMaxBootTries boots, and the next boot rolls back.
  for (int i = 1; i <= kMaxBootTries; ++i) OCHECK(onBootGuard(g, "a16f221d") == BootAction::Proceed && g.tries == i);
  OCHECK(onBootGuard(g, "a16f221d") == BootAction::Rollback);
  // The decision changes nothing by itself: a boot that cannot finish the rollback (or a failed switch) decides again.
  OCHECK(g.pending && g.tries == kMaxBootTries && !g.rolledBack);
  OCHECK(onBootGuard(g, "a16f221d") == BootAction::Rollback && g.tries == kMaxBootTries);
  rolledBackGuard(g);
  OCHECK(!g.pending && g.rolledBack && g.prevSlot == 1 && g.tries == 0);
  OCHECK(onBootGuard(g, "97f15374") == BootAction::Nothing);  // the old image boots: nothing left to do
  // A switch that keeps failing is tried kMaxRollbackAttempts times, then the trial ends: no endless flash erases.
  armGuard(g, 1, "eeeeeeee");
  for (int i = 0; i < kMaxBootTries; ++i) onBootGuard(g, "eeeeeeee");
  for (int attempt = 1; attempt <= kMaxRollbackAttempts; ++attempt) {
    OCHECK(onBootGuard(g, "eeeeeeee") == BootAction::Rollback);
    OCHECK(rollbackFailedGuard(g) == (attempt == kMaxRollbackAttempts));
  }
  OCHECK(!g.pending && !g.rolledBack && onBootGuard(g, "eeeeeeee") == BootAction::Nothing);
  // No old image to go back to: the trial just ends, with no notice.
  armGuard(g, 1, "dddddddd");
  for (int i = 0; i < kMaxBootTries; ++i) onBootGuard(g, "dddddddd");
  OCHECK(onBootGuard(g, "dddddddd") == BootAction::Rollback);
  giveUpGuard(g);
  OCHECK(!g.pending && !g.rolledBack && onBootGuard(g, "dddddddd") == BootAction::Nothing);
  // A build that confirms in time keeps its place.
  armGuard(g, 0, "bbbbbbbb");
  OCHECK(onBootGuard(g, "bbbbbbbb") == BootAction::Proceed);
  OCHECK(!guardConfirmDue(g, kConfirmAfterMs - 1) && guardConfirmDue(g, kConfirmAfterMs));
  confirmGuard(g);
  OCHECK(!g.pending && g.tries == 0 && !guardConfirmDue(g, 1u << 30));
  OCHECK(onBootGuard(g, "bbbbbbbb") == BootAction::Nothing);
  // A new trial clears the notice of the last rollback.
  g.rolledBack = true;
  armGuard(g, 1, "cccccccc");
  OCHECK(!g.rolledBack);
}

void testBootGuardNeverActsOnAnotherBuild() {
  BootGuard g;
  armGuard(g, 1, "a16f221d");
  OCHECK(onBootGuard(g, "97f15374") == BootAction::Stale);  // the install was cut short: the old image is still running
  OCHECK(!g.pending && g.tries == 0);
  armGuard(g, 1, "a16f221d");
  OCHECK(onBootGuard(g, "a16f221d+") == BootAction::Stale);  // a dirty local build is never the one that was installed
  armGuard(g, 1, "a16f221d");
  for (int i = 0; i < kMaxBootTries; ++i) onBootGuard(g, "a16f221d");
  OCHECK(onBootGuard(g, "97f15374") ==
         BootAction::Stale);  // even at the last try, another build never rolls anything back
  OCHECK(!g.rolledBack);
  armGuard(g, 1, "a16f221d");
  disarmGuard(g);  // the flash failed
  OCHECK(onBootGuard(g, "a16f221d") == BootAction::Nothing);
}

void testBootGuardRecord() {
  BootGuard g;
  armGuard(g, 1, "a16f221d");
  onBootGuard(g, "a16f221d");
  uint8_t blob[kGuardBlobSize];
  encodeGuard(g, blob);
  BootGuard r;
  OCHECK(decodeGuard(blob, sizeof blob, r) && r.pending && r.tries == 1 && r.prevSlot == 1 && r.rev == "a16f221d" &&
         !r.rolledBack);
  g.pending = false;
  g.rolledBack = true;
  encodeGuard(g, blob);
  OCHECK(decodeGuard(blob, sizeof blob, r) && !r.pending && r.rolledBack);
  // Anything that is not a record this firmware wrote is refused, so a damaged one can never cause a rollback.
  armGuard(g, 1, "a16f221d");
  encodeGuard(g, blob);
  OCHECK(!decodeGuard(blob, sizeof blob - 1, r) && !decodeGuard(nullptr, 0, r));
  for (size_t i = 0; i < sizeof blob; ++i) {
    uint8_t bad[kGuardBlobSize];
    std::memcpy(bad, blob, sizeof bad);
    bad[i] ^= 0x10;
    OCHECK(!decodeGuard(bad, sizeof bad, r));  // every single flipped bit pattern changes the checksum or the format
  }
  BootGuard junk;
  junk.pending = true;
  junk.rev = "not-hex!";
  encodeGuard(junk, blob);
  OCHECK(!decodeGuard(blob, sizeof blob, r));
}

}  // namespace

int runOtaTests(int& checks) {
  g_checks = g_fail = 0;
  testJson();
  testChannelIds();
  testSignatureComesFirst();
  testDocumentRules();
  testPolicyDropsWhatItDoesNotAccept();
  testOrderDuplicatesAndLimit();
  testRunningBuild();
  testManifestFromTheCiScript();
  testBootGuardTrialAndRollback();
  testBootGuardNeverActsOnAnotherBuild();
  testBootGuardRecord();
  testManifestWrittenByTheScript();
  checks += g_checks;
  return g_fail;
}
