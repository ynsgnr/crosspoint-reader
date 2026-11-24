// Host tests for opening a route through the phone: the order of the steps, and that every failure leaves nothing on.
#include <cstdio>
#include <string>
#include <vector>

#include "core/PhoneRoute.h"

using namespace mirror;

#define PCHECK(c)                                              \
  do {                                                         \
    ++checks;                                                  \
    if (!(c)) {                                                \
      ++failed;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                          \
  } while (0)

namespace {

struct FakeIo : PhoneRouteIo {
  std::vector<std::string> calls;
  bool memory = true, starts = true, bonded = true, link = true, answers = true, joins = true;
  gen::SyncOffer offer{gen::kSyncOfferReady, 0, "AndroidShare_1", "secret", "10.245.144.27", 8888, 600};
  uint32_t linkMs = 0, answerMs = 0, joinMs = 0;

  bool enoughMemory() override { return memory; }
  bool startBluetooth() override {
    calls.push_back("bt:on");
    return starts;
  }
  bool hasBond() override { return bonded; }
  bool waitLink(uint32_t ms) override {
    linkMs = ms;
    calls.push_back("link");
    return link;
  }
  void stopBluetooth() override { calls.push_back("bt:off"); }
  void sendRequest(uint8_t kind) override {
    calls.push_back(kind == gen::kSyncRequestStart ? "request:start" : "request:done");
  }
  bool waitOffer(gen::SyncOffer& out, uint32_t ms) override {
    answerMs = ms;
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
  void leaveWifi() override { calls.push_back("wifi:off"); }

  std::string trace() const {
    std::string s;
    for (const auto& c : calls) s += (s.empty() ? "" : " ") + c;
    return s;
  }
  bool shimOn() const { return trace().find("proxy:on") != std::string::npos; }
};

}  // namespace

int runPhoneRouteTests(int& checks) {
  int failed = 0;
  {  // the whole way: Bluetooth goes off before the radio is used for Wi-Fi
    FakeIo io;
    PCHECK(openPhoneRoute(io) == RouteResult::Open);
    PCHECK(io.trace() == "bt:on link request:start wait bt:off join:AndroidShare_1 proxy:on:10.245.144.27:8888");
    PCHECK(io.linkMs == 15000 && io.answerMs == 15000 && io.joinMs == 30000);
  }
  {  // timeouts can be tightened
    FakeIo io;
    RouteTimeouts t;
    t.linkMs = 1;
    t.answerMs = 2;
    t.joinMs = 3;
    openPhoneRoute(io, t);
    PCHECK(io.linkMs == 1 && io.answerMs == 2 && io.joinMs == 3);
  }
  {  // too little memory: nothing starts
    FakeIo io;
    io.memory = false;
    PCHECK(openPhoneRoute(io) == RouteResult::NoMemory && io.calls.empty());
  }
  {  // Bluetooth will not start
    FakeIo io;
    io.starts = false;
    PCHECK(openPhoneRoute(io) == RouteResult::NoPhone && io.trace() == "bt:on");
  }
  {  // no phone was ever paired: no waiting for one
    FakeIo io;
    io.bonded = false;
    PCHECK(openPhoneRoute(io) == RouteResult::NoPhone && io.trace() == "bt:on bt:off");
  }
  {  // the phone is not around: Bluetooth is switched off again and nothing was asked
    FakeIo io;
    io.link = false;
    PCHECK(openPhoneRoute(io) == RouteResult::NoPhone && io.trace() == "bt:on link bt:off");
  }
  {  // the app does not answer: it is told to drop anything it started (it may answer late), and everything is off
    FakeIo io;
    io.answers = false;
    PCHECK(openPhoneRoute(io) == RouteResult::NoAnswer);
    PCHECK(io.trace() == "bt:on link request:start wait request:done bt:off");
    PCHECK(!io.shimOn());
  }
  {  // a refusal starts nothing
    FakeIo io;
    io.offer = {gen::kSyncOfferRefused, gen::kSyncRefusedDisabled, "", "", "", 0, 0};
    PCHECK(openPhoneRoute(io) == RouteResult::Refused);
    PCHECK(io.trace() == "bt:on link request:start wait bt:off");
  }
  {  // unusable offers
    FakeIo stopped;
    stopped.offer.kind = gen::kSyncOfferStopped;
    PCHECK(openPhoneRoute(stopped) == RouteResult::Stopped && stopped.trace().find("join") == std::string::npos);
    FakeIo noPort;
    noPort.offer.proxy_port = 0;
    PCHECK(openPhoneRoute(noPort) == RouteResult::Stopped && noPort.trace().find("join") == std::string::npos);
    FakeIo noSsid;
    noSsid.offer.ssid.clear();
    PCHECK(openPhoneRoute(noSsid) == RouteResult::Stopped);
    PCHECK(noSsid.trace() == "bt:on link request:start wait request:done bt:off");
  }
  {  // the hotspot cannot be joined: Wi-Fi off again, the shim never on (the phone closes by itself after a minute)
    FakeIo io;
    io.joins = false;
    PCHECK(openPhoneRoute(io) == RouteResult::JoinFailed);
    PCHECK(io.trace() == "bt:on link request:start wait bt:off join:AndroidShare_1 wifi:off" && !io.shimOn());
  }
  {  // names for the log
    PCHECK(std::string(routeResultName(RouteResult::Open)) == "open" &&
           std::string(routeResultName(RouteResult::NoPhone)) == "no phone");
  }
  {  // frames
    const auto bytes = syncRequestFrame(gen::kSyncRequestStart, 7);
    EnvelopeStreamParser p;
    std::vector<Envelope> got;
    p.feed(bytes.data(), bytes.size(), got);
    PCHECK(got.size() == 1 && got[0].templateId == gen::kControlTemplateId &&
           got[0].schemaVersion == gen::kSyncSchema && got[0].has(gen::kFlagCommand) && got[0].seq == 7);
    Reader r(got[0].payload.data(), got[0].payload.size());
    gen::SyncRequest req;
    PCHECK(gen::SyncRequest::decode(r, req) && r.atEnd() && req.kind == gen::kSyncRequestStart);
  }
  {  // offers: only the sync schema on the control template as status, whole payloads only
    Envelope e;
    e.templateId = static_cast<uint8_t>(gen::kControlTemplateId);
    e.schemaVersion = static_cast<uint8_t>(gen::kSyncSchema);
    e.flags = gen::kFlagStatus;
    Writer w;
    gen::SyncOffer{gen::kSyncOfferReady, 0, "AndroidShare_7", "pw", "10.245.144.27", 8888, 600}.encode(w);
    e.payload = w.take();
    gen::SyncOffer o;
    PCHECK(syncOfferFrom(e, o) && o.ssid == "AndroidShare_7" && o.proxy_port == 8888 && o.valid_seconds == 600);
    Envelope wrongSchema = e;
    wrongSchema.schemaVersion = 0;
    PCHECK(!syncOfferFrom(wrongSchema, o));
    Envelope wrongTemplate = e;
    wrongTemplate.templateId = 1;
    PCHECK(!syncOfferFrom(wrongTemplate, o));
    Envelope notStatus = e;
    notStatus.flags = gen::kFlagCommand;
    PCHECK(!syncOfferFrom(notStatus, o));
    Envelope truncated = e;
    truncated.payload.pop_back();
    PCHECK(!syncOfferFrom(truncated, o));
    Envelope trailing = e;
    trailing.payload.push_back(0);
    PCHECK(!syncOfferFrom(trailing, o));
  }
  {  // capabilities: no templates, and the two features the phone needs
    const auto bytes = routeCapabilities("CrossPoint");
    Reader r(bytes.data(), bytes.size());
    gen::Capabilities c;
    PCHECK(gen::Capabilities::decode(r, c) && r.atEnd());
    PCHECK(c.protocol_version == gen::kProtocolVersion && c.templates.empty() && c.device_name == "CrossPoint");
    PCHECK((c.features & gen::kFeaturePairingCode) && (c.features & gen::kFeatureWifiSync) &&
           !(c.features & gen::kFeaturePhoneStatus));
  }
  return failed;
}
