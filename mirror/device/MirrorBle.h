#pragma once
#if defined(CROSSPOINT_MIRROR)
// GATT peripheral (NimBLE-Arduino), up only between start() and stop(). NimBLE callbacks run on its host task and
// only copy bytes into a guarded buffer or set flags; everything else happens on the activity's loop().
// Security: see mirror/README.md. Only a bonded phone may connect unless the pairing window is open.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "device/MirrorHid.h"

class NimBLEServer;
class NimBLECharacteristic;
class NimBLEConnInfo;

namespace mirror {

class MirrorBle {
 public:
  enum class StartResult { Ok, NoMemory, Failed };

  ~MirrorBle() { stop(); }  // the stack must not call back into a destroyed instance

  // The mirror service and the HID keyboard share one GATT server and bond. NoMemory when the heap cannot host the
  // stack.
  StartResult start(const char* deviceName, const std::vector<uint8_t>& capabilities);
  void stop();  // stops advertising, disconnects, deinitialises the stack

  // True once the link is encrypted and authenticated; nothing is sent or accepted before that.
  bool connected() const { return connected_.load(); }
  // Changes on every new connection, so a disconnect and reconnect between two loop passes is still noticed.
  uint32_t connectionEpoch() const { return epoch_.load(); }
  // Moves the phone's bytes into `out`; `overflowed` when some were dropped.
  bool drain(std::vector<uint8_t>& out, bool& overflowed);
  // Notifies one frame, split to the MTU.
  bool send(const std::vector<uint8_t>& frame);

  // Passkey to show while pairing, else -1.
  int32_t pairingPasskey() const { return passkey_.load(); }
  // Keyboard: false until the phone's HID host has subscribed, or when the link is down.
  bool keyboardReady() const { return hid_ && connected_.load() && hid_->ready(); }
  bool typeKey(uint8_t mods, uint8_t usage) { return keyboardReady() && hid_->tap(connHandle_.load(), mods, usage); }
  bool typeConsumer(uint16_t usage) { return keyboardReady() && hid_->tapConsumer(connHandle_.load(), usage); }
  bool wakePhone() { return keyboardReady() && hid_->wake(connHandle_.load()); }

  // True once after a connect, disconnect or pairing change.
  bool takeStatusChanged() { return statusChanged_.exchange(false); }
  int bondCount() const;
  void forgetBonds();

  // "Pair phone": unbonded phones may connect and pair for `durationMs`.
  void openPairing(uint32_t durationMs);
  bool pairingOpen() const;
  bool advertising() const { return advertisingState_.load() == 1; }
  bool advertisingFailed() const { return advertisingState_.load() == 0; }
  // Advertises while the window is open or a bond exists, and not connected. Call from loop().
  void service();

  // NimBLE callbacks (public for the callback classes in the .cpp).
  void onWrite(const uint8_t* data, size_t len, const NimBLEConnInfo& link);
  bool onConnected(const NimBLEConnInfo& link);  // false when the link was refused
  void onDisconnected(uint16_t handle, int reason);
  void onMtu(uint16_t mtu);
  uint32_t onPasskeyDisplay();
  void onAuthComplete(const NimBLEConnInfo& link);

 private:
  void dropOtherBonds(const NimBLEConnInfo& keep);
  void refreshBondCount();

  std::atomic<bool> running_{false};
  std::atomic<int> bondCount_{0};            // see bondCount()
  std::atomic<bool> pairingLatched_{false};  // a passkey was issued: the pairing may finish after the window closes
  std::atomic<uint32_t> pairingUntilMs_{0};
  bool minimalAdvertisement_ = false;      // fell back to the smallest advertisement after a failed start
  std::atomic<bool> linkUp_{false};        // a central is connected, secure or not
  bool advConnectable_ = true;             // the running advertisement accepts connections
  std::atomic<bool> tunePending_{false};   // connection parameters are due once the link is secure
  std::atomic<int> advertisingState_{-1};  // last advertising result: -1 not tried, 0 failed, 1 on
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> epoch_{0};
  std::atomic<bool> statusChanged_{false};
  std::atomic<int32_t> passkey_{-1};
  std::atomic<uint16_t> connHandle_{kNoConnection};
  std::atomic<uint16_t> mtu_{23};
  std::mutex mutex_;
  std::vector<uint8_t> inbound_;
  bool overflow_ = false;
  NimBLEServer* server_ = nullptr;
  std::unique_ptr<MirrorHid> hid_;
  NimBLECharacteristic* eventChar_ = nullptr;
  static constexpr size_t kInboundCap = 8192;
  static constexpr uint16_t kNoConnection = 0xFFFF;
};

}  // namespace mirror
#endif
