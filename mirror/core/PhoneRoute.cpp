#include "core/PhoneRoute.h"

namespace mirror {

const char* routeResultName(const RouteResult r) {
  switch (r) {
    case RouteResult::Open:
      return "open";
    case RouteResult::NoMemory:
      return "not enough memory";
    case RouteResult::NoPhone:
      return "no phone";
    case RouteResult::NoAnswer:
      return "the phone app did not answer";
    case RouteResult::Refused:
      return "the phone refused";
    case RouteResult::Stopped:
      return "the phone ended it";
    case RouteResult::JoinFailed:
      return "could not join the hotspot";
  }
  return "?";
}

RouteResult openPhoneRoute(PhoneRouteIo& io, const RouteTimeouts& t) {
  if (!io.enoughMemory()) return RouteResult::NoMemory;
  if (!io.startBluetooth()) return RouteResult::NoPhone;
  if (!io.hasBond() || !io.waitLink(t.linkMs)) {
    io.stopBluetooth();
    return RouteResult::NoPhone;
  }

  io.sendRequest(gen::kSyncRequestStart);
  gen::SyncOffer offer;
  if (!io.waitOffer(offer, t.answerMs)) {
    io.sendRequest(gen::kSyncRequestDone);  // a phone that answers late must not leave its hotspot up
    io.stopBluetooth();
    return RouteResult::NoAnswer;
  }
  if (offer.kind == gen::kSyncOfferRefused) {  // nothing was started on the phone
    io.stopBluetooth();
    return RouteResult::Refused;
  }
  if (offer.kind != gen::kSyncOfferReady || offer.ssid.empty() || offer.proxy_port == 0) {
    io.sendRequest(gen::kSyncRequestDone);
    io.stopBluetooth();
    return RouteResult::Stopped;
  }

  io.stopBluetooth();  // the heap for Wi-Fi and TLS; the phone keeps its hotspot until the proxy has been idle for a
                       // minute
  if (!io.joinWifi(offer, t.joinMs)) {
    io.leaveWifi();
    return RouteResult::JoinFailed;
  }
  io.enableProxy(offer);
  return RouteResult::Open;
}

std::vector<uint8_t> syncRequestFrame(const uint8_t kind, const uint16_t seq) {
  Writer w;
  gen::SyncRequest{kind}.encode(w);
  Envelope e;
  e.protocolVersion = gen::kProtocolVersion;
  e.templateId = static_cast<uint8_t>(gen::kControlTemplateId);
  e.schemaVersion = static_cast<uint8_t>(gen::kSyncSchema);
  e.flags = gen::kFlagCommand;
  e.seq = seq;
  e.payload = w.take();
  return encodeEnvelope(e);
}

bool syncOfferFrom(const Envelope& e, gen::SyncOffer& out) {
  if (e.templateId != gen::kControlTemplateId || e.schemaVersion != gen::kSyncSchema || !e.has(gen::kFlagStatus))
    return false;
  Reader r(e.payload.data(), e.payload.size());
  gen::SyncOffer offer;
  if (!gen::SyncOffer::decode(r, offer) || !r.atEnd()) return false;
  out = std::move(offer);
  return true;
}

std::vector<uint8_t> routeCapabilities(const std::string& deviceName) {
  gen::Capabilities c;
  c.protocol_version = gen::kProtocolVersion;
  c.max_payload = 512;
  c.device_name = deviceName;
  c.features = gen::kFeaturePairingCode |
               gen::kFeatureWifiSync;  // no phone_status, no templates: the phone sends control messages only
  Writer w;
  c.encode(w);
  return w.take();
}

}  // namespace mirror
