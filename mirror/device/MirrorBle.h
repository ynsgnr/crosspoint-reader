#pragma once
#if defined(CROSSPOINT_MIRROR)
// GATT peripheral (NimBLE-Arduino), up only between start() and stop(). NimBLE callbacks run on its host task and
// only copy bytes into a guarded buffer or set flags; everything else happens on the activity's loop().
// Security: LE Secure Connections only, MITM (passkey shown on screen), every characteristic and every notification
// subscription needs an encrypted + authenticated link, a resolvable private address, and only a bonded phone may
// connect unless the user opened the pairing window.
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

  // NoMemory when the heap cannot host the stack. `keyboard` adds the HID keyboard to the same GATT server and bond.
  StartResult start(const char* deviceName, const std::vector<uint8_t>& capabilities, bool keyboard);
  void stop();  // stops advertising, disconnects, deinitialises the stack
  bool running() const { return running_; }

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
  // Keyboard: false when it is off, not subscribed by the phone's HID host yet, or the link is down.
  bool keyboardReady() const { return hid_ && connected_.load() && hid_->ready(); }
  bool typeKey(uint8_t mods, uint8_t usage) { return keyboardReady() && hid_->tap(connHandle_.load(), mods, usage); }
  bool wakePhone() { return keyboardReady() && hid_->wake(connHandle_.load()); }

  // True once after a connect, disconnect or pairing change.
  bool takeStatusChanged() { return statusChanged_.exchange(false); }
  int bondCount() const;
  void forgetBonds();

  // Explicit "Pair phone": unbonded phones may connect and pair for `durationMs`. Bonded phones always may.
  void openPairing(uint32_t durationMs);
  bool pairingOpen() const;
  // Starts or stops advertising as needed (open window or a bond exists, and not connected). Call from loop().
  void service();

  // NimBLE callbacks (public for the callback classes in the .cpp).
  void onWrite(const uint8_t* data, size_t len, const NimBLEConnInfo& link);
  void onConnected(const NimBLEConnInfo& link);
  void onDisconnected();
  void onMtu(uint16_t mtu);
  uint32_t onPasskeyDisplay();
  void onAuthComplete(const NimBLEConnInfo& link);

 private:
  void dropOtherBonds(const NimBLEConnInfo& keep);

  bool running_ = false;
  std::atomic<uint32_t> pairingUntilMs_{0};
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> epoch_{0};
  std::atomic<bool> statusChanged_{false};
  std::atomic<int32_t> passkey_{-1};
  std::atomic<uint16_t> connHandle_{0xFFFF};
  std::atomic<uint16_t> mtu_{23};
  std::mutex mutex_;
  std::vector<uint8_t> inbound_;
  bool overflow_ = false;
  NimBLEServer* server_ = nullptr;
  std::unique_ptr<MirrorHid> hid_;
  NimBLECharacteristic* eventChar_ = nullptr;
  static constexpr size_t kInboundCap = 8192;
};

}  // namespace mirror
#endif
