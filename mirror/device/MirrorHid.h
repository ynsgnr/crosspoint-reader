#pragma once
#if defined(CROSSPOINT_MIRROR)
// BLE HID keyboard next to the mirror service. Built by hand: NimBLEHIDDevice leaves its characteristics readable
// without encryption. Keeps no input history; each key is sent and released.
#include <NimBLEDevice.h>

#include <atomic>
#include <cstdint>

namespace mirror {

class MirrorHid {
 public:
  // Call before NimBLEServer::start().
  void create(NimBLEServer* server);

  // True once the phone's HID host enabled input notifications on this link.
  bool ready() const { return subscribed_.load(); }
  void onLinkDown() { subscribed_ = false; }
  // Key down, then up. `mods` is the HID modifier byte.
  bool tap(uint16_t connHandle, uint8_t mods, uint8_t usage);
  // Consumer-control (media key) press and release.
  bool tapConsumer(uint16_t connHandle, uint16_t usage);
  // Shift tap, then the System Wake Up usage; which one wakes the phone depends on the phone.
  bool wake(uint16_t connHandle);

 private:
  bool sendKeys(uint16_t connHandle, uint8_t mods, uint8_t usage);
  bool sendSystem(uint16_t connHandle, uint8_t bits);

  // This NimBLE version has no subscriber count, so the subscription is tracked here.
  class Subscription final : public NimBLECharacteristicCallbacks {
   public:
    explicit Subscription(std::atomic<bool>& flag) : flag_(flag) {}
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t value) override { flag_ = (value & 1) != 0; }

   private:
    std::atomic<bool>& flag_;
  };

  std::atomic<bool> subscribed_{false};
  Subscription subscription_{subscribed_};
  NimBLECharacteristic* keyboardIn_ = nullptr;
  NimBLECharacteristic* consumerIn_ = nullptr;
  NimBLECharacteristic* systemIn_ = nullptr;
};

}  // namespace mirror
#endif
