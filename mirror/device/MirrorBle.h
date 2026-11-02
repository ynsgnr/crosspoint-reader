#pragma once
#if defined(CROSSPOINT_MIRROR)
// GATT peripheral (NimBLE-Arduino), up only between start() and stop(). NimBLE callbacks run on its host task and
// only copy bytes into a guarded buffer or set flags; everything else happens on the activity's loop().
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

class NimBLEServer;
class NimBLECharacteristic;

namespace mirror {

class MirrorBle {
 public:
  enum class StartResult { Ok, NoMemory, Failed };

  // NoMemory when the heap cannot host the stack.
  StartResult start(const char* deviceName, const std::vector<uint8_t>& capabilities);
  void stop();  // stops advertising, disconnects, deinitialises the stack
  bool running() const { return running_; }

  bool connected() const { return connected_.load(); }
  // Changes on every new connection, so a disconnect and reconnect between two loop passes is still noticed.
  uint32_t connectionEpoch() const { return epoch_.load(); }
  // Moves the phone's bytes into `out`; `overflowed` when some were dropped.
  bool drain(std::vector<uint8_t>& out, bool& overflowed);
  // Notifies one frame, split to the MTU.
  bool send(const std::vector<uint8_t>& frame);

  // Passkey to show while pairing, else -1.
  int32_t pairingPasskey() const { return passkey_.load(); }
  // True once after a connect, disconnect or pairing change.
  bool takeStatusChanged() { return statusChanged_.exchange(false); }
  int bondCount() const;
  void forgetBonds();

  // NimBLE callbacks (public for the callback classes in the .cpp).
  void onWrite(const uint8_t* data, size_t len);
  void onConnected(uint16_t handle);
  void onDisconnected();
  void onMtu(uint16_t mtu);
  uint32_t onPasskeyDisplay();
  void onAuthComplete(bool authenticated, uint16_t handle);

 private:
  bool running_ = false;
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
  NimBLECharacteristic* eventChar_ = nullptr;
  static constexpr size_t kInboundCap = 8192;
};

}  // namespace mirror
#endif
