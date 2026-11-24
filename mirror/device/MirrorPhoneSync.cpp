// "Sync over phone": MirrorActivity::syncOverPhone() runs core/PhoneSync.cpp against the real radio, the proxy shim and
// the stock OPDS sync. Blocking, like the firmware updater's steps: it runs from loop() with mutex_ released (see
// loop()).
#if defined(CROSSPOINT_MIRROR)
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's ip4_addr.h unless seen first (see
// MirrorUpdateActivity.cpp).
#include "network/HttpDownloader.h"
// clang-format off
#include "device/MirrorActivity.h"
// clang-format on
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/PhoneSync.h"
#include "device/ProxyHook.h"
#include "network/OpdsAutoSync.h"

using namespace mirror;

namespace {

constexpr uint32_t kWifiHeapReserve = 30000;  // the Wi-Fi stack comes up next to Bluetooth, before any TLS

uint32_t toHostOrder(const IPAddress& ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}

const char* refusalText(uint8_t code) {
  switch (code) {
    case gen::kSyncRefusedDisabled:
      return "Wi-Fi sync is off in the phone app";
    case gen::kSyncRefusedNoPermission:
      return "The phone app needs the Nearby devices permission";
    case gen::kSyncRefusedNoCellular:
      return "The phone has no mobile connection";
    case gen::kSyncRefusedHotspotFailed:
      return "The phone could not start its hotspot";
    case gen::kSyncRefusedBusy:
      return "The phone is already syncing";
    default:
      return "The phone refused";
  }
}

}  // namespace

void MirrorActivity::syncOverPhone() {
  struct Io final : PhoneSyncIo {
    MirrorActivity& a;
    opdssync::Summary summary;
    explicit Io(MirrorActivity& activity) : a(activity) {}

    void note(const char* text) {
      {
        std::lock_guard<std::mutex> lock(a.mutex_);
        a.note_ = text;
      }
      a.requestUpdateAndWait();
    }
    bool enoughMemory() override {
      return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP + kWifiHeapReserve &&
             ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC;
    }
    void stage(SyncStage s) override {
      switch (s) {
        case SyncStage::Asking:
          return note("Phone sync: asking the phone...");
        case SyncStage::Joining:
          return note("Phone sync: joining its hotspot...");
        case SyncStage::Syncing:
          return note("Phone sync: syncing through the phone...");
        case SyncStage::Closing:
          return note("Phone sync: closing...");
      }
    }
    void sendRequest(uint8_t kind) override {
      std::lock_guard<std::mutex> lock(a.mutex_);
      if (a.ble_.connected()) a.session_->sendSyncRequest(kind);
    }
    bool waitOffer(gen::SyncOffer& offer, uint32_t ms) override {
      const uint32_t until = millis() + ms;
      while (static_cast<int32_t>(until - millis()) > 0 && a.ble_.connected()) {
        std::vector<uint8_t> bytes;
        bool overflowed = false;
        if (a.ble_.drain(bytes, overflowed)) {
          std::lock_guard<std::mutex> lock(a.mutex_);
          a.session_->feed(bytes.data(), bytes.size());
          if (auto o = a.session_->takeSyncOffer()) {
            offer = std::move(*o);
            return true;
          }
        }
        delay(50);
      }
      return false;
    }
    // The internal heap does not hold Wi-Fi, Bluetooth and a TLS download together (serial log: "OSY Stopping: low
    // memory, max block 15860"): the radio stack goes off for the sync, the phone keeps its hotspot meanwhile (see its
    // WifiSyncController).
    void stopBluetooth() override { a.ble_.stop(); }
    void startBluetooth() override { a.startBle(); }
    bool waitLink(uint32_t ms) override {
      const uint32_t until = millis() + ms;
      while (static_cast<int32_t>(until - millis()) > 0) {
        a.ble_.service();  // advertises again once the stack is up
        if (a.ble_.connected()) return true;
        delay(100);
      }
      return false;
    }
    bool joinWifi(const gen::SyncOffer& offer, uint32_t ms) override {
      WiFi.persistent(false);
      WiFi.mode(WIFI_STA);
      WiFi.disconnect(true, true);
      delay(100);
      WiFi.begin(offer.ssid.c_str(), offer.passphrase.c_str());
      const uint32_t until = millis() + ms;
      while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(until - millis()) > 0) delay(100);
      LOG_INF("SYNC", "hotspot %s: %s", offer.ssid.c_str(), WiFi.status() == WL_CONNECTED ? "joined" : "not joined");
      return WiFi.status() == WL_CONNECTED;
    }
    void enableProxy(const gen::SyncOffer& offer) override {
      IPAddress proxy;
      if (!proxy.fromString(offer.proxy_host.c_str())) proxy = WiFi.gatewayIP();
      const uint32_t mask = toHostOrder(WiFi.subnetMask());
      proxyhook::enable(toHostOrder(proxy), offer.proxy_port, toHostOrder(WiFi.localIP()) & mask, mask);
    }
    void disableProxy() override { proxyhook::disable(); }
    bool runSync() override {
      // opdssync::syncNow() joins "the last Wi-Fi" first, which returns at once because the hotspot is already up.
      LOG_INF("SYNC", "before the sync: free %u, max block %u (Wi-Fi and Bluetooth are both up)",
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      summary = opdssync::syncNow();
      LOG_INF("SYNC", "after the sync: free %u, max block %u, lowest free %u", static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()));
      LOG_INF("SYNC", "servers=%u downloaded=%u failed=%u auth_failed=%d", summary.servers, summary.downloaded,
              summary.failed, summary.authFailed);
      return !summary.wifiFailed && !summary.authFailed && summary.failed == 0;
    }
    void leaveWifi() override {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  } io(*this);

  const SyncResult result = runPhoneSync(io);
  proxyhook::disable();  // whatever happened above, no connection of this firmware stays tunnelled
  char text[96];
  switch (result.outcome) {
    case SyncOutcome::Done:
      if (io.summary.servers == 0)
        snprintf(text, sizeof(text), "Phone sync ran, but no OPDS sync feed is set up");
      else
        snprintf(text, sizeof(text), "Phone sync done: %u new book(s)", io.summary.downloaded);
      break;
    case SyncOutcome::SyncFailed:
      if (io.summary.authFailed)
        snprintf(text, sizeof(text), "Phone sync: the catalog needs a password");
      else
        snprintf(text, sizeof(text), "Phone sync: %u failed, %u new", io.summary.failed, io.summary.downloaded);
      break;
    case SyncOutcome::NoMemory:
      snprintf(text, sizeof(text), "Not enough free memory: go Back and try again");
      break;
    case SyncOutcome::NoAnswer:
      snprintf(text, sizeof(text), "The phone app did not answer (needs the new version)");
      break;
    case SyncOutcome::Refused:
      snprintf(text, sizeof(text), "%s", refusalText(result.refusal));
      break;
    case SyncOutcome::Stopped:
      snprintf(text, sizeof(text), "The phone ended the sync");
      break;
    case SyncOutcome::JoinFailed:
      snprintf(text, sizeof(text), "Could not join the phone's hotspot");
      break;
  }
  LOG_INF("SYNC", "%s", text);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    note_ = text;
  }
  requestUpdate();
}
#endif
