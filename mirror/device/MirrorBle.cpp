#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorBle.h"

#include <Logging.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#include <algorithm>

#include "protocol_gen.h"

namespace mirror {
namespace {

// Unmeasured estimate of the block the NimBLE host needs; tighten after measuring on hardware.
constexpr size_t kMinLargestBlock = 24 * 1024;

class ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(MirrorBle& b) : b_(b) {}
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    b_.onConnected(info.getConnHandle());
    // 15-30 ms interval so typed letters show quickly.
    s->updateConnParams(info.getConnHandle(), 12, 24, 0, 400);
    s->setDataLen(info.getConnHandle(), 251);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { b_.onDisconnected(); }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { b_.onMtu(mtu); }
  uint32_t onPassKeyDisplay() override { return b_.onPasskeyDisplay(); }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    b_.onAuthComplete(info.isAuthenticated(), info.getConnHandle());
  }

 private:
  MirrorBle& b_;
};

class DataCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit DataCallbacks(MirrorBle& b) : b_(b) {}
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    b_.onWrite(v.data(), v.size());
  }

 private:
  MirrorBle& b_;
};

// NimBLE keeps raw pointers to these, so they live for the whole program.
ServerCallbacks* serverCallbacks = nullptr;
DataCallbacks* dataCallbacks = nullptr;

}  // namespace

MirrorBle::StartResult MirrorBle::start(const char* deviceName, const std::vector<uint8_t>& capabilities) {
  if (running_) return StartResult::Ok;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < kMinLargestBlock) {
    LOG_ERR("MIRROR", "not enough heap for BLE: largest block %u",
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return StartResult::NoMemory;
  }
  if (!NimBLEDevice::init(deviceName)) return StartResult::Failed;
  NimBLEDevice::setMTU(gen::kRequestedMtu);
  // Display-only IO: the phone types the code shown on screen.
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  if (!serverCallbacks) serverCallbacks = new ServerCallbacks(*this);
  if (!dataCallbacks) dataCallbacks = new DataCallbacks(*this);

  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(serverCallbacks, false);
  server_->advertiseOnDisconnect(true);
  NimBLEService* svc = server_->createService(gen::kUuidService);
  NimBLECharacteristic* data = svc->createCharacteristic(
      gen::kUuidData, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN, 512);
  data->setCallbacks(dataCallbacks);
  eventChar_ = svc->createCharacteristic(gen::kUuidEvent, NIMBLE_PROPERTY::NOTIFY, 512);
  NimBLECharacteristic* caps = svc->createCharacteristic(
      gen::kUuidCaps, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN, 512);
  caps->setValue(capabilities.data(), capabilities.size());
  server_->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(gen::kUuidService);  // Companion Device Manager filters on this
  adv->setName(deviceName);
  adv->enableScanResponse(true);
  if (!adv->start()) {
    NimBLEDevice::deinit(true);
    server_ = nullptr;
    eventChar_ = nullptr;
    return StartResult::Failed;
  }
  {
    std::lock_guard<std::mutex> g(mutex_);
    inbound_.reserve(kInboundCap);  // grown here, not on the NimBLE host task
  }
  running_ = true;
  connected_ = false;
  statusChanged_ = true;
  LOG_INF("MIRROR", "advertising as %s, %d bonds", deviceName, NimBLEDevice::getNumBonds());
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
  connected_ = false;
  passkey_ = -1;
  std::lock_guard<std::mutex> g(mutex_);
  inbound_.clear();
  overflow_ = false;
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

void MirrorBle::onWrite(const uint8_t* data, size_t len) {
  std::lock_guard<std::mutex> g(mutex_);
  if (inbound_.size() + len > kInboundCap) {
    overflow_ = true;  // the loop turns this into a resync request; the stream parser skips the torn frame
    return;
  }
  inbound_.insert(inbound_.end(), data, data + len);
}

void MirrorBle::onConnected(uint16_t handle) {
  {
    std::lock_guard<std::mutex> g(mutex_);  // bytes of a previous link must not reach the new session
    inbound_.clear();
    overflow_ = false;
  }
  connHandle_ = handle;
  mtu_ = 23;
  epoch_++;
  connected_ = true;
  statusChanged_ = true;
}

void MirrorBle::onDisconnected() {
  connected_ = false;
  passkey_ = -1;
  statusChanged_ = true;
}

void MirrorBle::onMtu(uint16_t mtu) { mtu_ = mtu; }

uint32_t MirrorBle::onPasskeyDisplay() {
  const uint32_t code = esp_random() % 1000000;
  passkey_ = static_cast<int32_t>(code);
  statusChanged_ = true;
  return code;
}

void MirrorBle::onAuthComplete(bool authenticated, uint16_t handle) {
  passkey_ = -1;
  statusChanged_ = true;
  // Encryption alone is not enough: a Just Works bond has no passkey and could be made by anyone nearby.
  if (!authenticated && server_) server_->disconnect(handle);
}

}  // namespace mirror
#endif
