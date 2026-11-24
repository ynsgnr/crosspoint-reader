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
  bool memory = true, answers = true, joins = true, syncs = true, linkBack = true;
  gen::SyncOffer offer{gen::kSyncOfferReady, 0, "AndroidShare_1", "secret", "10.245.144.27", 8888, 600};
  uint32_t askedMs = 0, joinMs = 0, relinkMs = 0;

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
  void stopBluetooth() override { calls.push_back("bt:off"); }
  void startBluetooth() override { calls.push_back("bt:on"); }
  bool waitLink(uint32_t ms) override {
    relinkMs = ms;
    calls.push_back("link");
    return linkBack;
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
    // Bluetooth goes off before the radio is used for Wi-Fi and TLS, and is back (with "done" over it) after everything
    // is undone.
    SCHECK(
        io.trace() ==
        "stage0 request:start wait stage1 bt:off join:AndroidShare_1 stage2 proxy:on:10.245.144.27:8888 sync proxy:off "
        "wifi:off stage3 bt:on link request:done");
    SCHECK(io.askedMs == 20000 && io.joinMs == 30000 && io.relinkMs == 20000);
  }
  {  // a failed sync is still undone and reported
    FakeIo io;
    io.syncs = false;
    SCHECK(runPhoneSync(io).outcome == SyncOutcome::SyncFailed);
    SCHECK(io.trace().find("proxy:off wifi:off stage3 bt:on link request:done") != std::string::npos);
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
    SCHECK(io.trace() ==
           "stage0 request:start wait stage1 bt:off join:AndroidShare_1 wifi:off bt:on link request:done");
  }
  {  // the phone does not come back after the sync: nothing is sent into the void (it closes its hotspot when idle)
    FakeIo io;
    io.linkBack = false;
    SCHECK(runPhoneSync(io).outcome == SyncOutcome::Done);
    SCHECK(io.trace().find("bt:on link") != std::string::npos && io.trace().find("request:done") == std::string::npos);
  }
  {  // unusable offers
    FakeIo io;
    io.offer.kind = gen::kSyncOfferStopped;
    SCHECK(runPhoneSync(io).outcome == SyncOutcome::Stopped);
    FakeIo noPort;
    noPort.offer.proxy_port = 0;
    SCHECK(runPhoneSync(noPort).outcome == SyncOutcome::Stopped && noPort.trace().find("join") == std::string::npos &&
           noPort.trace().find("bt:off") == std::string::npos);  // an unusable offer never costs the link
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
