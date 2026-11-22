#include "tests/ota_tests.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "ota/BootGuard.h"
#include "ota/OtaConfig.h"
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

// Accepts exactly the signature bytes "GOOD" and counts how often it was asked.
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

const std::string kSha(64, 'a');

// <id> <rev> <built> <size> <sha256> <title>
std::string line(const std::string& id, const std::string& rev = "a16f221d", const std::string& built = "1763100000",
                 const std::string& size = "3000000", const std::string& sha = kSha,
                 const std::string& title = "A pull request") {
  return id + " " + rev + " " + built + " " + size + " " + sha + " " + title + "\n";
}

Trust open(const std::string& text, std::vector<Channel>& out, bool good = true) {
  static const FakeVerifier v;
  return openManifest(text, good ? kGood : kBad, 4, v, out);
}

void testManifest() {
  std::vector<Channel> c;
  OCHECK(open(line("develop", "97f15374") +
                  line("pr-19", "a16f221d", "1763100001", "3000001", kSha, "List template: on the device \xC3\xA9"),
              c) == Trust::Ok);
  OCHECK(c.size() == 2 && c[0].id == "develop" && c[0].rev == "97f15374" && c[0].size == 3000000 &&
         c[0].built == 1763100000);
  OCHECK(c[1].id == "pr-19" && c[1].sha256 == kSha &&
         c[1].title == "List template: on the device \xC3\xA9");  // spaces and UTF-8 kept
  OCHECK(open("", c) == Trust::BadSignature && c.empty());
}

void testSignatureComesFirst() {
  std::vector<Channel> c;
  const FakeVerifier v;
  // Text that is not a manifest, with a bad signature: refused as unsigned, never read.
  OCHECK(openManifest("not a manifest", kBad, 4, v, c) == Trust::BadSignature && v.calls == 1);
  OCHECK(openManifest("not a manifest", kGood, 4, v, c) == Trust::BadFormat);
  OCHECK(openManifest(line("pr-1"), nullptr, 0, v, c) == Trust::BadSignature);
  OCHECK(open(line("pr-1"), c, false) == Trust::BadSignature && c.empty());
  OCHECK(open(std::string(kMaxManifestBytes + 1, 'x'), c) ==
         Trust::BadSignature);  // oversize: not even offered to the verifier
}

void testBadFormat() {
  std::vector<Channel> c;
  const std::string good = line("pr-2");
  const char* const bad[] = {
      "pr-1 a16f221d 1763100000 3000000\n",             // fields missing
      "pr-1 a16f221d 1763100000 3000000 aaaa title\n",  // short hash
      "main a16f221d 1763100000 3000000 title\n",       // not a channel id
  };
  for (const char* b : bad) OCHECK(open(b, c) == Trust::BadFormat && c.empty());
  OCHECK(open(line("pr-0"), c) == Trust::BadFormat && open(line("pr-1234567"), c) == Trust::BadFormat);
  OCHECK(open(line("pr-1", "XYZ"), c) == Trust::BadFormat && open(line("pr-1", "A16F221D"), c) == Trust::BadFormat);
  OCHECK(open(line("pr-1", "a16f221d", "0"), c) == Trust::BadFormat &&
         open(line("pr-1", "a16f221d", "12x"), c) == Trust::BadFormat);
  OCHECK(open(line("pr-1", "a16f221d", "1763100000", "1000"), c) == Trust::BadFormat);  // too small to be an image
  OCHECK(open(line("pr-1", "a16f221d", "1763100000", "3000000", std::string(64, 'A')), c) == Trust::BadFormat);
  OCHECK(open(line("pr-1", "a16f221d", "1763100000", "3000000", kSha, ""), c) == Trust::BadFormat);  // no title
  OCHECK(open(line("pr-1", "a16f221d", "1763100000", "3000000", kSha, " leading space"), c) == Trust::BadFormat);
  OCHECK(open(line("pr-1", "a16f221d", "1763100000", "3000000", kSha, std::string(97, 'x')), c) == Trust::BadFormat);
  OCHECK(open(line("pr-1", "a16f221d", "1763100000", "3000000", kSha, "tab\there"), c) == Trust::BadFormat);
  OCHECK(open(good.substr(0, good.size() - 1), c) == Trust::BadFormat);  // the last line needs its newline too
  OCHECK(open(good + good, c) == Trust::BadFormat);                      // the same id twice
  std::string many;
  for (size_t i = 1; i <= kMaxChannels + 1; ++i) many += line("pr-" + std::to_string(i));
  OCHECK(open(many, c) == Trust::BadFormat);
  OCHECK(open(many.substr(0, many.find('\n', many.size() / 2 + 1) + 1), c) == Trust::Ok && c.size() <= kMaxChannels);
}

void testIdsAndRunningBuild() {
  OCHECK(isChannelId("develop") && isChannelId("pr-1") && isChannelId("pr-123456"));
  for (const char* id :
       {"", "main", "pr-", "pr-0", "pr-012", "pr-1234567", "pr-1a", "PR-1", "pr--1", "develop2", "../pr-1"})
    OCHECK(!isChannelId(id));
  Channel c;
  c.rev = "a16f221d";
  OCHECK(!differsFromRunning(c, "a16f221d") && differsFromRunning(c, "97f15374"));
  OCHECK(differsFromRunning(c, "a16f221d+"));  // uncommitted changes: never the same build
  OCHECK(differsFromRunning(c, ""));
}

void testNamingConvention() {
  // CI (mirror/scripts/ota/publish.sh) publishes under exactly these names.
  OCHECK(manifestUrl("x4pro") ==
         "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/channels-x4pro.txt");
  OCHECK(signatureUrl("x4pro") == manifestUrl("x4pro") + ".sig");
  OCHECK(imageUrl("pr-19", "x4pro") ==
         "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/pr-19-x4pro.bin");
}

// What mirror/scripts/ota/publish.sh writes (mirror/scripts/ota/test.sh checks the fixture against the script).
void testManifestWrittenByTheScript() {
  std::ifstream f(std::string(MIRROR_OTA_FIXTURE_DIR) + "/channels-x4pro.txt");
  std::stringstream ss;
  ss << f.rdbuf();
  std::vector<Channel> c;
  OCHECK(!ss.str().empty() && open(ss.str(), c) == Trust::Ok && c.size() == 3);
  OCHECK(c[0].id == "develop" && c[1].id == "pr-19" &&
         c[2].id == "pr-7");  // develop first, then newest pull request first
  OCHECK(c[1].title == "List template on the device" && c[1].size == 600000 && c[1].rev == "a16f221d");
}

void testGuardTrialAndRollback() {
  BootGuard g;
  OCHECK(onBootGuard(g, "a16f221d") == BootAction::Nothing);
  armGuard(g, 1, "a16f221d");
  OCHECK(g.pending && g.tries == 0 && g.prevSlot == 1);
  // A build that never confirms gets kMaxBootTries boots, and the next boot rolls back.
  for (int i = 1; i <= kMaxBootTries; ++i) OCHECK(onBootGuard(g, "a16f221d") == BootAction::Proceed && g.tries == i);
  OCHECK(onBootGuard(g, "a16f221d") == BootAction::Rollback);
  OCHECK(g.pending && onBootGuard(g, "a16f221d") ==
                          BootAction::Rollback);  // deciding changes nothing: a failed switch decides again
  endTrial(g);
  OCHECK(onBootGuard(g, "97f15374") == BootAction::Nothing);  // the old image boots: nothing left to do
  // A build that confirms in time keeps its place.
  armGuard(g, 0, "bbbbbbbb");
  OCHECK(onBootGuard(g, "bbbbbbbb") == BootAction::Proceed);
  OCHECK(!guardConfirmDue(g, kConfirmAfterMs - 1) && guardConfirmDue(g, kConfirmAfterMs));
  endTrial(g);
  OCHECK(!guardConfirmDue(g, 1u << 30) && onBootGuard(g, "bbbbbbbb") == BootAction::Nothing);
}

void testGuardIgnoresTimerWakes() {
  BootGuard g;
  armGuard(g, 1, "a16f221d");
  // Background syncs wake on a timer, run a few seconds and sleep: any number of them leaves the trial untouched.
  for (int i = 0; i < 10; ++i) OCHECK(onBootGuard(g, "a16f221d", true) == BootAction::Nothing);
  OCHECK(g.pending && g.tries == 0);
  // Real boots still count, and timer wakes between them neither add nor subtract.
  OCHECK(onBootGuard(g, "a16f221d") == BootAction::Proceed && g.tries == 1);
  OCHECK(onBootGuard(g, "a16f221d", true) == BootAction::Nothing && g.tries == 1);
  for (int i = 2; i <= kMaxBootTries; ++i) OCHECK(onBootGuard(g, "a16f221d") == BootAction::Proceed && g.tries == i);
  OCHECK(onBootGuard(g, "a16f221d", true) == BootAction::Nothing);  // no rollback decided on a timer wake
  OCHECK(onBootGuard(g, "a16f221d") == BootAction::Rollback);       // the next real boot still rolls back
  // A timer wake of another build does not drop the record either.
  BootGuard h;
  armGuard(h, 1, "a16f221d");
  OCHECK(onBootGuard(h, "97f15374", true) == BootAction::Nothing && h.pending);
}

void testGuardNeverActsOnAnotherBuild() {
  BootGuard g;
  armGuard(g, 1, "a16f221d");
  OCHECK(onBootGuard(g, "97f15374") == BootAction::Stale &&
         !g.pending);  // the install was cut short: the old image is running
  armGuard(g, 1, "a16f221d");
  OCHECK(onBootGuard(g, "a16f221d+") == BootAction::Stale);  // a dirty local build is never the one that was installed
  armGuard(g, 1, "a16f221d");
  for (int i = 0; i < kMaxBootTries; ++i) onBootGuard(g, "a16f221d");
  OCHECK(onBootGuard(g, "97f15374") ==
         BootAction::Stale);  // even at the last try, another build never rolls anything back
}

void testGuardFailedSwitchIsBounded() {
  BootGuard g;
  armGuard(g, 1, "eeeeeeee");
  for (int i = 0; i < kMaxBootTries; ++i) onBootGuard(g, "eeeeeeee");
  for (int attempt = 1; attempt <= kMaxRollbackAttempts; ++attempt) {
    OCHECK(onBootGuard(g, "eeeeeeee") == BootAction::Rollback);
    OCHECK(rollbackFailedGuard(g) == (attempt == kMaxRollbackAttempts));  // no endless flash erases
  }
  OCHECK(!g.pending && onBootGuard(g, "eeeeeeee") == BootAction::Nothing);
}

void testGuardRecord() {
  BootGuard g, r;
  armGuard(g, 1, "a16f221d");
  onBootGuard(g, "a16f221d");
  uint8_t blob[kGuardBlobSize];
  encodeGuard(g, blob);
  OCHECK(decodeGuard(blob, sizeof blob, r) && r.pending && r.tries == 1 && r.prevSlot == 1 && r.rev == "a16f221d");
  endTrial(g);
  encodeGuard(g, blob);
  OCHECK(decodeGuard(blob, sizeof blob, r) && !r.pending);
  // Anything that is not a record this firmware wrote is refused, so it can never cause a rollback.
  armGuard(g, 1, "a16f221d");
  encodeGuard(g, blob);
  OCHECK(!decodeGuard(blob, sizeof blob - 1, r) && !decodeGuard(nullptr, 0, r));
  uint8_t bad[kGuardBlobSize];
  std::memcpy(bad, blob, sizeof bad);
  bad[0] = 2;
  OCHECK(!decodeGuard(bad, sizeof bad, r));
  BootGuard junk;
  junk.pending = true;
  junk.rev = "not-hex!";
  encodeGuard(junk, blob);
  OCHECK(!decodeGuard(blob, sizeof blob, r));
}

}  // namespace

int runOtaTests(int& checks) {
  g_checks = g_fail = 0;
  testManifest();
  testSignatureComesFirst();
  testBadFormat();
  testIdsAndRunningBuild();
  testNamingConvention();
  testManifestWrittenByTheScript();
  testGuardTrialAndRollback();
  testGuardIgnoresTimerWakes();
  testGuardNeverActsOnAnotherBuild();
  testGuardFailedSwitchIsBounded();
  testGuardRecord();
  checks += g_checks;
  return g_fail;
}
