// Host tests for the order of the phone sync sequence, with a recording fake for the platform.
#include <cstdio>
#include <string>
#include <vector>

#include "core/PhoneSync.h"

using namespace mirror;

#define SCHECK(c)                                              \
  do {                                                         \
    ++checks;                                                  \
    if (!(c)) {                                                \
      ++failed;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                          \
  } while (0)

namespace {

struct FakeIo : PhoneSyncIo {
  std::vector<std::string> calls;
  bool memory = true, answers = true, joins = true, syncs = true;
  gen::SyncOffer offer{gen::kSyncOfferReady, 0, "AndroidShare_1", "secret", "10.245.144.27", 8888, 600};
  uint32_t askedMs = 0, joinMs = 0;

  bool enoughMemory() override { return memory; }
  void stage(SyncStage s) override { calls.push_back("stage" + std::to_string(static_cast<int>(s))); }
  void sendRequest(uint8_t kind) override {
    calls.push_back(kind == gen::kSyncRequestStart ? "request:start" : "request:done");
  }
  bool waitOffer(gen::SyncOffer& out, uint32_t ms) override {
    askedMs = ms;
    calls.push_back("wait");
    if (answers) out = offer;
    return answers;
  }
  bool joinWifi(const gen::SyncOffer& o, uint32_t ms) override {
    joinMs = ms;
    calls.push_back("join:" + o.ssid);
    return joins;
  }
  void enableProxy(const gen::SyncOffer& o) override {
    calls.push_back("proxy:on:" + o.proxy_host + ":" + std::to_string(o.proxy_port));
  }
  void disableProxy() override { calls.push_back("proxy:off"); }
  bool runSync() override {
    calls.push_back("sync");
    return syncs;
  }
  void leaveWifi() override { calls.push_back("wifi:off"); }

  std::string trace() const {
    std::string s;
    for (const auto& c : calls) s += (s.empty() ? "" : " ") + c;
    return s;
  }
};

}  // namespace

int runPhoneSyncTests(int& checks) {
  int failed = 0;
  {  // the whole way, in order, and everything undone
    FakeIo io;
    const SyncResult r = runPhoneSync(io);
    SCHECK(r.outcome == SyncOutcome::Done);
    SCHECK(io.trace() ==
           "stage0 request:start wait stage1 join:AndroidShare_1 stage2 proxy:on:10.245.144.27:8888 sync proxy:off "
           "wifi:off stage3 request:done");
    SCHECK(io.askedMs == 20000 && io.joinMs == 30000);
  }
  {  // a failed sync is still undone and reported
    FakeIo io;
    io.syncs = false;
    SCHECK(runPhoneSync(io).outcome == SyncOutcome::SyncFailed);
    SCHECK(io.trace().find("proxy:off wifi:off stage3 request:done") != std::string::npos);
  }
  {  // no answer: nothing joined, and the phone is told (it may answer late)
    FakeIo io;
    io.answers = false;
    SCHECK(runPhoneSync(io).outcome == SyncOutcome::NoAnswer);
    SCHECK(io.trace() == "stage0 request:start wait request:done");
  }
  {  // a refusal names its reason and starts nothing
    FakeIo io;
    io.offer = {gen::kSyncOfferRefused, gen::kSyncRefusedDisabled, "", "", "", 0, 0};
    const SyncResult r = runPhoneSync(io);
    SCHECK(r.outcome == SyncOutcome::Refused && r.refusal == gen::kSyncRefusedDisabled);
    SCHECK(io.trace() == "stage0 request:start wait");
  }
  {  // a hotspot that cannot be joined: Wi-Fi off again, phone told, proxy never on
    FakeIo io;
    io.joins = false;
    SCHECK(runPhoneSync(io).outcome == SyncOutcome::JoinFailed);
    SCHECK(io.trace() == "stage0 request:start wait stage1 join:AndroidShare_1 wifi:off request:done");
  }
  {  // unusable offers
    FakeIo io;
    io.offer.kind = gen::kSyncOfferStopped;
    SCHECK(runPhoneSync(io).outcome == SyncOutcome::Stopped);
    FakeIo noPort;
    noPort.offer.proxy_port = 0;
    SCHECK(runPhoneSync(noPort).outcome == SyncOutcome::Stopped && noPort.trace().find("join") == std::string::npos);
    FakeIo noSsid;
    noSsid.offer.ssid.clear();
    SCHECK(runPhoneSync(noSsid).outcome == SyncOutcome::Stopped);
  }
  {  // too little heap: not even a request
    FakeIo io;
    io.memory = false;
    SCHECK(runPhoneSync(io).outcome == SyncOutcome::NoMemory && io.calls.empty());
  }
  return failed;
}
