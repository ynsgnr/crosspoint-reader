#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorBle.h"

#include <Logging.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <host/ble_hs.h>
#include <nvs.h>

#include <algorithm>

#include "protocol_gen.h"

// Private to NimBLE's GATT server (ble_gatt_priv.h): permissions of every Client Characteristic Configuration
// descriptor, i.e. who may subscribe to notifications. The default is open.
extern "C" void ble_gatts_set_clt_cfg_perm_flags(uint8_t flags);

namespace mirror {
namespace {

// Bumped whenever the security model changes; bonds made under an older one are deleted at the next start.
constexpr uint8_t kSecurityEpoch = 3;
constexpr uint8_t kCccdFlags = BLE_ATT_F_READ | BLE_ATT_F_WRITE | BLE_ATT_F_READ_ENC | BLE_ATT_F_WRITE_ENC |
                               BLE_ATT_F_READ_AUTHEN | BLE_ATT_F_WRITE_AUTHEN;

bool linkSecure(const NimBLEConnInfo& link) { return link.isEncrypted() && link.isAuthenticated(); }

// True when the stored epoch is current; otherwise records it and returns false.
bool securityEpochCurrent() {
  nvs_handle_t h;
  if (nvs_open("mirror", NVS_READWRITE, &h) != ESP_OK) return false;
  uint8_t stored = 0;
  const bool current = nvs_get_u8(h, "sec_epoch", &stored) == ESP_OK && stored == kSecurityEpoch;
  if (!current) nvs_set_u8(h, "sec_epoch", kSecurityEpoch), nvs_commit(h);
  nvs_close(h);
  return current;
}

// Unmeasured estimate of the block the NimBLE host needs; tighten after measuring on hardware.
constexpr size_t kMinLargestBlock = 24 * 1024;

class ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(MirrorBle& b) : b_(b) {}
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    b_.onConnected(info);
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

}  // namespace

MirrorBle::StartResult MirrorBle::start(const char* deviceName, const std::vector<uint8_t>& capabilities,
                                        bool keyboard) {
  if (running_) return StartResult::Ok;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < kMinLargestBlock) {
    LOG_ERR("MIRROR", "not enough heap for BLE: largest block %u",
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return StartResult::NoMemory;
  }
  if (!NimBLEDevice::init(deviceName)) return StartResult::Failed;
  NimBLEDevice::setMTU(gen::kRequestedMtu);
  // Bonding + MITM + LE Secure Connections, and nothing weaker: legacy pairing is refused outright.
  // Display-only IO: the phone types the passkey shown on screen, so a Just Works bond cannot be made.
  NimBLEDevice::setSecurityAuth(true, true, true);
  ble_hs_cfg.sm_sc_only = 1;
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  // Resolvable private address (the IRK goes to the bonded phone), so the device cannot be tracked by its address.
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RPA_RANDOM_DEFAULT);
  // Subscribing to notifications needs the same authenticated link as reading or writing.
  ble_gatts_set_clt_cfg_perm_flags(kCccdFlags);
  // Bonds from before this security model are not trusted: forget them so the phone pairs again.
  if (!securityEpochCurrent()) NimBLEDevice::deleteAllBonds();

  if (!serverCallbacks) serverCallbacks = new ServerCallbacks(*this);
  if (!dataCallbacks) dataCallbacks = new DataCallbacks(*this);

  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(serverCallbacks, false);
  server_->advertiseOnDisconnect(false);  // service() decides: only with an open pairing window or a bond
  NimBLEService* svc = server_->createService(gen::kUuidService);
  NimBLECharacteristic* data = svc->createCharacteristic(
      gen::kUuidData, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN, 512);
  data->setCallbacks(dataCallbacks);
  // The subscription itself (CCCD) is authenticated through ble_gatts_set_clt_cfg_perm_flags above.
  eventChar_ = svc->createCharacteristic(gen::kUuidEvent, NIMBLE_PROPERTY::NOTIFY, 512);
  NimBLECharacteristic* caps = svc->createCharacteristic(
      gen::kUuidCaps, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN, 512);
  caps->setValue(capabilities.data(), capabilities.size());
  if (keyboard) {
    hid_ = std::make_unique<MirrorHid>();
    hid_->create(server_);
  }
  server_->start();

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
  LOG_INF("MIRROR", "up, %d bonds", NimBLEDevice::getNumBonds());
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
  connected_ = false;
  passkey_ = -1;
  pairingUntilMs_ = 0;
  std::lock_guard<std::mutex> g(mutex_);
  inbound_.clear();
  overflow_ = false;
}

void MirrorBle::openPairing(uint32_t durationMs) {
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
  const bool wanted = !connected_ && (pairingOpen() || NimBLEDevice::getNumBonds() > 0);
  if (wanted && !adv->isAdvertising()) adv->start();
  if (!wanted && adv->isAdvertising()) adv->stop();
  if (pairingUntilMs_ != 0 && !pairingOpen()) {
    pairingUntilMs_ = 0;  // window expired
    statusChanged_ = true;
  }
}

bool MirrorBle::drain(std::vector<uint8_t>& out, bool& overflowed) {
  std::lock_guard<std::mutex> g(mutex_);
  overflowed = overflow_;
  overflow_ = false;
  if (inbound_.empty()) return false;
  out.swap(inbound_);
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

void MirrorBle::onConnected(const NimBLEConnInfo& link) {
  // An unknown phone may only connect while the user has the pairing window open.
  if (!NimBLEDevice::isBonded(link.getIdAddress()) && !pairingOpen()) {
    server_->disconnect(link.getConnHandle());
    return;
  }
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
}

void MirrorBle::onDisconnected() {
  connected_ = false;
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
  passkey_ = -1;
  statusChanged_ = true;
  // Encrypted alone is not enough: a Just Works bond has no passkey and could be made by anyone nearby.
  if (!link.isEncrypted() || !link.isAuthenticated() || !link.isBonded()) {
    if (server_) server_->disconnect(link.getConnHandle());
    return;
  }
  dropOtherBonds(link);  // one phone at a time: pairing a new one replaces the old
  pairingUntilMs_ = 0;
  epoch_++;
  connected_ = true;
}

void MirrorBle::dropOtherBonds(const NimBLEConnInfo& keep) {
  for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; --i) {
    const NimBLEAddress addr = NimBLEDevice::getBondedAddress(i);
    if (addr != keep.getIdAddress()) NimBLEDevice::deleteBond(addr);
  }
}

}  // namespace mirror
#endif
