#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorBle.h"

#include <Logging.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <host/ble_hs.h>

#include <algorithm>

#include "device/MirrorSettings.h"
#include "protocol_gen.h"

// NimBLE-private (ble_gatt_priv.h): permissions of every notification subscription (CCCD). The default is open.
extern "C" void ble_gatts_set_clt_cfg_perm_flags(uint8_t flags);

namespace mirror {
namespace {

// Bumped whenever the security model changes; bonds made under an older one are deleted at the next start.
constexpr uint8_t kSecurityEpoch = 3;
constexpr uint8_t kCccdFlags = BLE_ATT_F_READ | BLE_ATT_F_WRITE | BLE_ATT_F_READ_ENC | BLE_ATT_F_WRITE_ENC |
                               BLE_ATT_F_READ_AUTHEN | BLE_ATT_F_WRITE_AUTHEN;

bool linkSecure(const NimBLEConnInfo& link) { return link.isEncrypted() && link.isAuthenticated(); }

// Unmeasured estimate of the block the NimBLE host needs; tighten after measuring on hardware.
constexpr size_t kMinLargestBlock = 24 * 1024;

class ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(MirrorBle& b) : b_(b) {}
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    if (!b_.onConnected(info)) return;
    // 15-30 ms interval so typed letters show quickly.
    s->updateConnParams(info.getConnHandle(), 12, 24, 0, 400);
    s->setDataLen(info.getConnHandle(), 251);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { b_.onDisconnected(); }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { b_.onMtu(mtu); }
  uint32_t onPassKeyDisplay() override { return b_.onPasskeyDisplay(); }
  void onAuthenticationComplete(NimBLEConnInfo& info) override { b_.onAuthComplete(info); }

 private:
  MirrorBle& b_;
};

class DataCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit DataCallbacks(MirrorBle& b) : b_(b) {}
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    const NimBLEAttValue v = c->getValue();
    b_.onWrite(v.data(), v.size(), info);
  }

 private:
  MirrorBle& b_;
};

// NimBLE keeps raw pointers to these, so they live for the whole program.
ServerCallbacks* serverCallbacks = nullptr;
DataCallbacks* dataCallbacks = nullptr;

// Bonding + MITM + LE Secure Connections only (legacy pairing is refused). Display-only IO: the phone types the passkey
// shown on screen, so a Just Works bond cannot be made. Private address: the device cannot be tracked.
void configureSecurity() {
  NimBLEDevice::setSecurityAuth(true, true, true);
  ble_hs_cfg.sm_sc_only = 1;
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  if (!NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RPA_RANDOM_DEFAULT)) LOG_ERR("MIRROR", "private address unavailable");
  ble_gatts_set_clt_cfg_perm_flags(kCccdFlags);  // subscribing needs the same authenticated link as reading
  if (!settings::securityEpochCurrent(kSecurityEpoch)) NimBLEDevice::deleteAllBonds();  // old model: pair again
}

}  // namespace

MirrorBle::StartResult MirrorBle::start(const char* deviceName, const std::vector<uint8_t>& capabilities,
                                        bool keyboard) {
  if (running_) return StartResult::Ok;
  keyboard_ = keyboard;
  minimalAdvertisement_ = false;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < kMinLargestBlock) {
    LOG_ERR("MIRROR", "not enough heap for BLE: largest block %u",
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return StartResult::NoMemory;
  }
  if (!NimBLEDevice::init(deviceName)) return StartResult::Failed;
  NimBLEDevice::setMTU(gen::kRequestedMtu);
  configureSecurity();

  if (!serverCallbacks) serverCallbacks = new ServerCallbacks(*this);
  if (!dataCallbacks) dataCallbacks = new DataCallbacks(*this);

  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(serverCallbacks, false);
  server_->advertiseOnDisconnect(false);  // service() decides: only with an open pairing window or a bond
  NimBLEService* svc = server_->createService(gen::kUuidService);
  NimBLECharacteristic* data = svc->createCharacteristic(
      gen::kUuidData, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN, 512);
  data->setCallbacks(dataCallbacks);
  eventChar_ = svc->createCharacteristic(gen::kUuidEvent, NIMBLE_PROPERTY::NOTIFY, 512);
  NimBLECharacteristic* caps = svc->createCharacteristic(
      gen::kUuidCaps, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN, 512);
  caps->setValue(capabilities.data(), capabilities.size());
  if (keyboard) {
    hid_ = std::make_unique<MirrorHid>();
    hid_->create(server_);
  }
  if (!server_->start()) {
    LOG_ERR("MIRROR", "GATT server failed to start (keyboard %d)", keyboard);
    NimBLEDevice::deinit(true);
    server_ = nullptr;
    eventChar_ = nullptr;
    hid_.reset();
    return StartResult::Failed;
  }

  // Only the service UUID (the Companion Device Manager filters on it) and a generic name: no content, no serial.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(gen::kUuidService);
  if (keyboard) {
    adv->addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));  // the phone's HID host looks for this
    adv->setAppearance(0x03C1);                                      // keyboard
  }
  adv->setName(deviceName);
  adv->enableScanResponse(true);
  {
    std::lock_guard<std::mutex> g(mutex_);
    inbound_.reserve(kInboundCap);  // grown here, not on the NimBLE host task
  }
  running_ = true;
  connected_ = false;
  statusChanged_ = true;
  pairingUntilMs_ = 0;
  LOG_INF("MIRROR", "up, %d bonds, keyboard %s", NimBLEDevice::getNumBonds(), keyboard ? "on" : "off");
  return StartResult::Ok;
}

void MirrorBle::stop() {
  if (!running_) return;
  running_ = false;
  if (NimBLEDevice::getAdvertising()->isAdvertising()) NimBLEDevice::getAdvertising()->stop();
  if (server_) {
    for (auto handle : server_->getPeerDevices()) server_->disconnect(handle);
  }
  NimBLEDevice::deinit(true);  // frees server/advertising objects; bonds stay in NVS
  server_ = nullptr;
  eventChar_ = nullptr;
  hid_.reset();
  linkUp_ = false;
  advertisingState_ = -1;
  connected_ = false;
  passkey_ = -1;
  pairingUntilMs_ = 0;
  std::lock_guard<std::mutex> g(mutex_);
  inbound_.clear();
  overflow_ = false;
}

void MirrorBle::openPairing(uint32_t durationMs) {
  LOG_INF("MIRROR", "pairing window open for %u s", static_cast<unsigned>(durationMs / 1000));
  pairingUntilMs_ = millis() + durationMs;
  statusChanged_ = true;
}

bool MirrorBle::pairingOpen() const {
  const uint32_t until = pairingUntilMs_.load();
  return until != 0 && static_cast<int32_t>(until - millis()) > 0;
}

void MirrorBle::service() {
  if (!running_) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  const bool wanted = !linkUp_ && (pairingOpen() || NimBLEDevice::getNumBonds() > 0);
  if (wanted && !adv->isAdvertising()) {
    bool ok = adv->start();
    if (!ok && keyboard_ && !minimalAdvertisement_) {
      // The keyboard adds a service UUID and an appearance; retry with only the mirror service UUID.
      adv->removeServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));
      adv->setAppearance(0);
      minimalAdvertisement_ = true;
      ok = adv->start();
      LOG_INF("MIRROR", "retry with the minimal advertisement: %s", ok ? "on" : "failed");
    }
    const int started = ok ? 1 : 0;
    if (started != advertisingState_.load()) LOG_INF("MIRROR", "advertising %s", started ? "on" : "FAILED to start");
    advertisingState_ = started;
  }
  if (!wanted && adv->isAdvertising()) {
    adv->stop();
    advertisingState_ = -1;
    LOG_INF("MIRROR", "advertising off");
  }
  if (pairingUntilMs_ != 0 && !pairingOpen()) {
    LOG_INF("MIRROR", "pairing window closed");
    pairingUntilMs_ = 0;  // window expired
    statusChanged_ = true;
  }
}

bool MirrorBle::drain(std::vector<uint8_t>& out, bool& overflowed) {
  std::lock_guard<std::mutex> g(mutex_);
  overflowed = overflow_;
  overflow_ = false;
  if (inbound_.empty()) return false;
  out.assign(inbound_.begin(),
             inbound_.end());  // a copy, so inbound_ keeps its reserved capacity (no allocation on the host task)
  inbound_.clear();
  return true;
}

bool MirrorBle::send(const std::vector<uint8_t>& frame) {
  if (!connected_ || !eventChar_) return false;
  const size_t chunk = std::max<size_t>(20, mtu_.load() - 3);
  for (size_t off = 0; off < frame.size(); off += chunk) {
    const size_t n = std::min(chunk, frame.size() - off);
    bool ok = false;
    for (int attempt = 0; attempt < 5 && !ok; ++attempt) {
      ok = eventChar_->notify(frame.data() + off, n, connHandle_.load());
      if (!ok) vTaskDelay(pdMS_TO_TICKS(5));  // mbuf pool momentarily empty
    }
    if (!ok) return false;
  }
  return true;
}

int MirrorBle::bondCount() const { return running_ ? NimBLEDevice::getNumBonds() : 0; }
void MirrorBle::forgetBonds() {
  NimBLEDevice::deleteAllBonds();
  statusChanged_ = true;
}

void MirrorBle::onWrite(const uint8_t* data, size_t len, const NimBLEConnInfo& link) {
  // The stack already refuses this write on an open link; checking again keeps the rule local and testable.
  if (!linkSecure(link)) {
    if (server_) server_->disconnect(link.getConnHandle());
    return;
  }
  std::lock_guard<std::mutex> g(mutex_);
  if (inbound_.size() + len > kInboundCap) {
    overflow_ = true;  // the loop turns this into a resync request; the stream parser skips the torn frame
    return;
  }
  inbound_.insert(inbound_.end(), data, data + len);
}

bool MirrorBle::onConnected(const NimBLEConnInfo& link) {
  const bool bonded = NimBLEDevice::isBonded(link.getIdAddress());
  LOG_INF("MIRROR", "central connected, bonded %d, window open %d", bonded, pairingOpen());
  // An unknown phone may only connect while the user has the pairing window open.
  if (!bonded && !pairingOpen()) {
    server_->disconnect(link.getConnHandle());
    return false;
  }
  linkUp_ = true;
  {
    std::lock_guard<std::mutex> g(mutex_);  // bytes of a previous link must not reach the new session
    inbound_.clear();
    overflow_ = false;
  }
  connHandle_ = link.getConnHandle();
  mtu_ = 23;
  // Ask for encryption (bonded phone) or start pairing (open window) instead of waiting for the phone to try.
  if (!linkSecure(link)) NimBLEDevice::startSecurity(link.getConnHandle());
  statusChanged_ = true;
  return true;
}

void MirrorBle::onDisconnected() {
  LOG_INF("MIRROR", "central disconnected");
  linkUp_ = false;
  connected_ = false;
  connHandle_ = kNoConnection;
  if (hid_) hid_->onLinkDown();
  passkey_ = -1;
  statusChanged_ = true;
}

void MirrorBle::onMtu(uint16_t mtu) { mtu_ = mtu; }

uint32_t MirrorBle::onPasskeyDisplay() {
  // A passkey is only asked for when a (re)pairing runs; outside the window nothing may start one, not even a
  // phone that is already bonded.
  if (!pairingOpen()) {
    if (server_) server_->disconnect(connHandle_.load());
    return 0;
  }
  const uint32_t code = esp_random() % 1000000;
  passkey_ = static_cast<int32_t>(code);
  statusChanged_ = true;
  return code;
}

void MirrorBle::onAuthComplete(const NimBLEConnInfo& link) {
  LOG_INF("MIRROR", "auth: encrypted %d, authenticated %d, bonded %d", link.isEncrypted(), link.isAuthenticated(),
          link.isBonded());
  passkey_ = -1;
  statusChanged_ = true;
  // Encrypted alone is not enough: a Just Works bond has no passkey and could be made by anyone nearby.
  if (!link.isEncrypted() || !link.isAuthenticated() || !link.isBonded()) {
    NimBLEDevice::deleteBond(link.getIdAddress());  // the stack may already have stored the weak bond
    pairingUntilMs_ = 0;                            // a failed attempt closes the window: retry on purpose
    if (server_) server_->disconnect(link.getConnHandle());
    return;
  }
  dropOtherBonds(link);  // one phone at a time: pairing a new one replaces the old
  pairingUntilMs_ = 0;
  if (!connected_.exchange(true)) epoch_++;  // a repeated authentication on the same link is not a new connection
}

void MirrorBle::dropOtherBonds(const NimBLEConnInfo& keep) {
  for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; --i) {
    const NimBLEAddress addr = NimBLEDevice::getBondedAddress(i);
    if (addr != keep.getIdAddress()) NimBLEDevice::deleteBond(addr);
  }
}

}  // namespace mirror
#endif
