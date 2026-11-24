#pragma once
// A way online through the phone, for a sync that finds no Wi-Fi it can join (device/PhoneRoute.cpp). The device has no
// screen for this: a timer wake runs it unattended, and "Sync Now" shows only a popup. The sequence, with the platform
// behind an interface so the order and every failure are host-tested:
//   start Bluetooth -> wait for the bonded phone -> ask -> wait for its offer -> switch Bluetooth OFF -> join its
//   hotspot -> turn the proxy shim on.
// Bluetooth goes off before the radio does Wi-Fi and TLS because the internal heap holds only two of the three (serial
// log: "OSY Stopping: low memory (free 44540, max block 15860)" with all three up). The phone keeps its hotspot through
// the gap and closes it after a minute without traffic (its WifiSyncController); the device never needs to say done.
// Any failure leaves everything off and the sync is simply skipped; the next timer wake or "Sync Now" tries again.
#include <cstdint>
#include <string>
#include <vector>

#include "core/Envelope.h"
#include "generated/protocol_gen.h"

namespace mirror {

enum class RouteResult {
  Open,        // Wi-Fi is up, every connection now goes through the phone
  NoMemory,    // not enough free heap for Bluetooth, Wi-Fi and a download
  NoPhone,     // Bluetooth would not start, no phone is paired, or the phone did not connect
  NoAnswer,    // the phone's app did not answer (not running, too old)
  Refused,     // the phone said no (Wi-Fi sync off, no permission, no mobile data, hotspot failed, busy)
  Stopped,     // the phone ended it, or its offer was unusable
  JoinFailed,  // the hotspot could not be joined
};
const char* routeResultName(RouteResult r);

struct RouteTimeouts {
  uint32_t linkMs = 15000;    // a bonded phone reconnects within seconds when the device advertises
  uint32_t answerMs = 15000;  // cellular and the hotspot take a few seconds to come up
  uint32_t joinMs = 30000;
};

class PhoneRouteIo {
 public:
  virtual ~PhoneRouteIo() = default;
  virtual bool enoughMemory() = 0;
  virtual bool startBluetooth() = 0;  // false: the stack could not start
  virtual bool hasBond() = 0;         // a phone was paired (known once the stack runs)
  virtual bool waitLink(uint32_t ms) = 0;
  virtual void stopBluetooth() = 0;
  virtual void sendRequest(uint8_t kind) = 0;                      // gen::kSyncRequest*
  virtual bool waitOffer(gen::SyncOffer& offer, uint32_t ms) = 0;  // keeps the link served while it waits
  virtual bool joinWifi(const gen::SyncOffer& offer, uint32_t ms) = 0;
  virtual void enableProxy(const gen::SyncOffer& offer) = 0;  // the shim: all connections now go through the phone
  virtual void leaveWifi() = 0;
};

RouteResult openPhoneRoute(PhoneRouteIo& io, const RouteTimeouts& t = {});

// The frames of a link that has no Session (no templates, nothing to draw).
std::vector<uint8_t> syncRequestFrame(uint8_t kind, uint16_t seq);
// True for the phone's offer: control template, sync schema, flags.status, a payload that decodes completely.
bool syncOfferFrom(const Envelope& e, gen::SyncOffer& out);
// Capabilities without templates, so the phone sends nothing but control messages; it needs pairing_code (its stored
// confirmation then opens the link) and wifi_sync (it will not offer otherwise).
std::vector<uint8_t> routeCapabilities(const std::string& deviceName);

}  // namespace mirror
