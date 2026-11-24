// The phone as a way online for the OPDS sync (core/PhoneRoute.h has the sequence and the reasoning). Registered with
// opdssync::setNetworkFallback at start-up, so src/network/OpdsAutoSync.cpp needs no mirror code: it only asks "any
// other way online?" when no saved Wi-Fi network can be joined, on a timer wake or on "Sync Now".
#if defined(CROSSPOINT_MIRROR)
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's ip4_addr.h unless seen first (see
// MirrorUpdateActivity.cpp).
#include "core/PhoneRoute.h"

#include <BlePageTurner.h>
#include <Logging.h>
#include <WiFi.h>

#include <string>
#include <vector>

#include "core/Envelope.h"
#include "device/MirrorBle.h"
#include "device/ProxyHook.h"
#include "network/HttpDownloader.h"
#include "network/OpdsAutoSync.h"

namespace {

constexpr char kDeviceName[] = "E-Ink Reader";  // as the Mirror screen: the phone's pairing knows the device by it
constexpr uint32_t kWifiHeapReserve = 30000;    // the Wi-Fi stack comes up next to Bluetooth, before any TLS
constexpr uint32_t kRadioFreeMs = 3000;         // the page turner's radio gives its heap back over a few passes

uint32_t toHostOrder(const IPAddress& ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}

// Talks to the phone with no Session and no screen: only the sync messages are read, everything else the phone sends is
// dropped.
class DeviceRouteIo final : public mirror::PhoneRouteIo {
 public:
  bool enoughMemory() override {
    return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP + kWifiHeapReserve &&
           ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC;
  }

  bool startBluetooth() override {
    // The page turner owns the radio on some screens (the stack cannot be started twice).
    for (const uint32_t until = millis() + kRadioFreeMs; !bleturner::beforeScreenChange(0, false);) {
      if (static_cast<int32_t>(until - millis()) <= 0) return false;
      delay(50);
    }
    return ble_.start(kDeviceName, mirror::routeCapabilities(kDeviceName)) == mirror::MirrorBle::StartResult::Ok;
  }

  bool hasBond() override { return ble_.bondCount() > 0; }

  bool waitLink(uint32_t ms) override {
    for (const uint32_t until = millis() + ms; static_cast<int32_t>(until - millis()) > 0; delay(50)) {
      ble_.service();  // advertises, so the phone's presence service or its reconnect finds the device
      if (ble_.connected()) return true;
    }
    return false;
  }

  void stopBluetooth() override { ble_.stop(); }

  void sendRequest(uint8_t kind) override {
    if (ble_.connected()) ble_.send(mirror::syncRequestFrame(kind, seq_++));
  }

  bool waitOffer(mirror::gen::SyncOffer& offer, uint32_t ms) override {
    for (const uint32_t until = millis() + ms; static_cast<int32_t>(until - millis()) > 0 && ble_.connected();
         delay(50)) {
      std::vector<uint8_t> bytes;
      bool overflowed = false;
      if (!ble_.drain(bytes, overflowed)) continue;
      std::vector<mirror::Envelope> frames;
      parser_.feed(bytes.data(), bytes.size(), frames);
      for (const auto& frame : frames)
        if (mirror::syncOfferFrom(frame, offer)) return true;
    }
    return false;
  }

  bool joinWifi(const mirror::gen::SyncOffer& offer, uint32_t ms) override {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.begin(offer.ssid.c_str(), offer.passphrase.c_str());
    for (const uint32_t until = millis() + ms;
         WiFi.status() != WL_CONNECTED && static_cast<int32_t>(until - millis()) > 0;)
      delay(100);
    LOG_INF("PHONE", "hotspot %s: %s", offer.ssid.c_str(), WiFi.status() == WL_CONNECTED ? "joined" : "not joined");
    return WiFi.status() == WL_CONNECTED;
  }

  void enableProxy(const mirror::gen::SyncOffer& offer) override {
    IPAddress proxy;
    if (!proxy.fromString(offer.proxy_host.c_str())) proxy = WiFi.gatewayIP();
    const uint32_t mask = toHostOrder(WiFi.subnetMask());
    mirror::proxyhook::enable(toHostOrder(proxy), offer.proxy_port, toHostOrder(WiFi.localIP()) & mask, mask);
  }

  void leaveWifi() override {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

 private:
  mirror::MirrorBle ble_;  // its destructor stops the stack, so no failure path can leave Bluetooth running
  mirror::EnvelopeStreamParser parser_;
  uint16_t seq_ = 1;
};

class PhoneRoute final : public opdssync::NetworkFallback {
 public:
  bool open() override {
    DeviceRouteIo io;
    const mirror::RouteResult result = mirror::openPhoneRoute(io);
    LOG_INF("PHONE", "way online through the phone: %s", mirror::routeResultName(result));
    if (result != mirror::RouteResult::Open)
      mirror::proxyhook::disable();  // nothing may stay tunnelled after a failed round
    return result == mirror::RouteResult::Open;
  }
};

PhoneRoute g_route;
// Static initialisation: the hook is a plain pointer in OpdsAutoSync.cpp, so the order between the two files does not
// matter.
[[maybe_unused]] const bool g_registered = (opdssync::setNetworkFallback(&g_route), true);

}  // namespace
#endif
