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

// The callback objects outlive one MirrorBle (the stack keeps them), so they point at the current instance and are
// re-bound on every start(); a stale reference to a destroyed instance aborted the firmware on the second visit.
class ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  void bind(MirrorBle* b) { b_ = b; }
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    if (b_) b_->onConnected(info);  // connection tuning waits for a secure link (see service())
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
    if (b_) b_->onDisconnected(info.getConnHandle(), reason);
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override {
    if (b_) b_->onMtu(mtu);
  }
  uint32_t onPassKeyDisplay() override { return b_ ? b_->onPasskeyDisplay() : 0; }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (b_) b_->onAuthComplete(info);
  }

 private:
  MirrorBle* b_ = nullptr;
};

class DataCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  void bind(MirrorBle* b) { b_ = b; }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    if (!b_) return;
    const NimBLEAttValue v = c->getValue();
    b_->onWrite(v.data(), v.size(), info);
  }

 private:
  MirrorBle* b_ = nullptr;
};

// NimBLE keeps raw pointers to these, so they live for the whole program.
ServerCallbacks* serverCallbacks = nullptr;
DataCallbacks* dataCallbacks = nullptr;

// Bonding + MITM + LE Secure Connections only (legacy pairing is refused). Display-only IO: the phone types the passkey
// shown on screen, so a Just Works bond cannot be made. The public address is used: on this controller advertising
// with the host-based private address fails (HCI error 0x12, seen on hardware), so the device is not untraceable.
void configureSecurity() {
  NimBLEDevice::setSecurityAuth(true, true, true);
  ble_hs_cfg.sm_sc_only = 1;
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  ble_gatts_set_clt_cfg_perm_flags(kCccdFlags);  // subscribing needs the same authenticated link as reading
  if (!settings::securityEpochCurrent(kSecurityEpoch)) NimBLEDevice::deleteAllBonds();  // old model: pair again
}

}  // namespace

MirrorBle::StartResult MirrorBle::start(const char* deviceName, const std::vector<uint8_t>& capabilities,
                                        bool keyboard) {
  if (running_) return StartResult::Ok;
  keyboard_ = keyboard;
  minimalAdvertisement_ = false;
  advConnectable_ = true;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < kMinLargestBlock) {
    LOG_ERR("MIRROR", "not enough heap for BLE: largest block %u",
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return StartResult::NoMemory;
  }
  if (!NimBLEDevice::init(deviceName)) return StartResult::Failed;
  NimBLEDevice::setMTU(gen::kRequestedMtu);
  configureSecurity();

  if (!serverCallbacks) serverCallbacks = new ServerCallbacks();
  if (!dataCallbacks) dataCallbacks = new DataCallbacks();
  serverCallbacks->bind(this);
  dataCallbacks->bind(this);

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
    // No keyboard appearance: Android then treats the device as a keyboard, shows its own PIN and answers the passkey
    // request with it instead of asking the user for the code on our screen.
    adv->addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));  // the phone's HID host looks for this
  }
  adv->enableScanResponse(true);  // before setName: the name goes to the scan response, not the full main packet
  adv->setName(deviceName);
  {
    std::lock_guard<std::mutex> g(mutex_);
    inbound_.reserve(kInboundCap);  // grown here, not on the NimBLE host task
  }
  running_ = true;
  connected_ = false;
  statusChanged_ = true;
  pairingUntilMs_ = 0;
  LOG_INF("MIRROR", "build %s %s, %d bonds, keyboard %s", __DATE__, __TIME__, NimBLEDevice::getNumBonds(),
          keyboard ? "on" : "off");
  return StartResult::Ok;
}

void MirrorBle::stop() {
  if (!running_) return;
  running_ = false;
  if (NimBLEDevice::getAdvertising()->isAdvertising()) NimBLEDevice::getAdvertising()->stop();
  if (server_) {
    for (auto handle : server_->getPeerDevices()) server_->disconnect(handle);
    // Deinitialising while a link is still closing races the host task (a heap assert with two phones in Flowe's
    // firmware): wait for every link to end, at most 3 s.
    for (int waited = 0; server_->getConnectedCount() > 0 && waited < 150; ++waited) vTaskDelay(pdMS_TO_TICKS(20));
  }
  NimBLEDevice::deinit(true);  // frees server/advertising objects; bonds stay in NVS
  serverCallbacks->bind(nullptr);
  dataCallbacks->bind(nullptr);
  LOG_INF("MIRROR", "stopped, heap free %u, largest block %u",
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));  // watch for leaks per visit
  server_ = nullptr;
  eventChar_ = nullptr;
  hid_.reset();
  linkUp_ = false;
  tunePending_ = false;
  advConnectable_ = true;
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
  // Not advertising to a phone that is already linked would hide the device from the phone's companion-device scan
  // (association, presence), so a secure link keeps a non-connectable advertisement going: a second connection
  // attempt (the phone's HID host and its app both reconnect on their own) would only be refused and retried.
  const bool bonded = NimBLEDevice::getNumBonds() > 0;
  const bool wanted = linkUp_ ? (bonded && connected_) : (pairingOpen() || bonded);
  const bool connectable = !linkUp_;
  if (wanted && adv->isAdvertising() && connectable != advConnectable_) adv->stop();  // the mode changes with the link
  if (wanted && !adv->isAdvertising()) {
    adv->setConnectableMode(connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON);
    advConnectable_ = connectable;
    bool ok = adv->start();
    if (!ok && keyboard_ && !minimalAdvertisement_) {
      // The keyboard adds a service UUID; retry with only the mirror service UUID.
      adv->removeServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));
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
  // Connection parameters and data length change with link-layer procedures that must not overlap the phone starting
  // encryption (a MIC failure ended reconnects on hardware), so they are requested once the link is secure.
  if (tunePending_.exchange(false) && server_ && connected_) {
    const uint16_t handle = connHandle_.load();
    server_->updateConnParams(handle, 12, 24, 0,
                              500);  // 15-30 ms so typed letters show quickly; 5 s supervision timeout
    server_->setDataLen(handle, 251);
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
  LOG_INF("MIRROR", "central connected, id %s, bonded %d, bonds stored %d, window open %d",
          link.getIdAddress().toString().c_str(), bonded, NimBLEDevice::getNumBonds(), pairingOpen());
  const auto refuse = [&] {
    server_->disconnect(link.getConnHandle());
    return false;
  };
  if (linkUp_ && connHandle_.load() != link.getConnHandle()) return refuse();  // one phone at a time
  // Nothing is readable or writable without an encrypted, authenticated link, and a new pairing needs the window, so
  // a connection is accepted whenever a bond exists: a bonded phone that connects with a private address the stack
  // cannot resolve yet must not be turned away.
  if (!pairingOpen() && NimBLEDevice::getNumBonds() == 0) return refuse();
  linkUp_ = true;
  {
    std::lock_guard<std::mutex> g(mutex_);  // bytes of a previous link must not reach the new session
    inbound_.clear();
    overflow_ = false;
  }
  connHandle_ = link.getConnHandle();
  mtu_ = 23;
  // The phone secures the link itself: it starts encryption for a bonded device and pairing when its app asks for it.
  statusChanged_ = true;
  return true;
}

void MirrorBle::onDisconnected(uint16_t handle, int reason) {
  LOG_INF("MIRROR", "central disconnected, reason 0x%x", static_cast<unsigned>(reason));
  if (handle != connHandle_.load()) return;  // a refused second link says nothing about the accepted one
  linkUp_ = false;
  connected_ = false;
  tunePending_ = false;
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
    LOG_INF("MIRROR", "passkey requested outside the pairing window: refused");
    if (server_) server_->disconnect(connHandle_.load());
    return 0;
  }
  LOG_INF("MIRROR", "passkey requested, showing it");
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
    // Only a pairing in progress loses its bond and closes the window. An existing bond survives a failed encryption
    // (a radio glitch, a phone that restarted its stack): deleting it forced a full re-pair on hardware.
    if (pairingOpen()) {
      if (NimBLEDevice::isBonded(link.getIdAddress())) NimBLEDevice::deleteBond(link.getIdAddress());  // a weak bond
      pairingUntilMs_ = 0;  // a failed attempt closes the window: retry on purpose
    }
    if (server_) server_->disconnect(link.getConnHandle());
    return;
  }
  if (pairingOpen()) dropOtherBonds(link);  // one phone at a time: pairing a new one replaces the old
  pairingUntilMs_ = 0;
  if (!connected_.exchange(true)) epoch_++;  // a repeated authentication on the same link is not a new connection
  tunePending_ = true;
}

void MirrorBle::dropOtherBonds(const NimBLEConnInfo& keep) {
  for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; --i) {
    const NimBLEAddress addr = NimBLEDevice::getBondedAddress(i);
    if (addr != keep.getIdAddress()) NimBLEDevice::deleteBond(addr);
  }
}

}  // namespace mirror
#endif
