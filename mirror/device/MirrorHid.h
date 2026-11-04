#pragma once
#if defined(CROSSPOINT_MIRROR)
// BLE HID-over-GATT keyboard (HID service, battery, device information) next to the mirror service. Built by hand
// rather than with NimBLEHIDDevice because that helper leaves its characteristics readable without encryption; here
// every characteristic needs the same authenticated link as the mirror. Keeps no input history: each key is sent and
// released.
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
  // Wakes a sleeping phone: Shift tap, then the System Wake Up usage. Which one works depends on the phone.
  bool wake(uint16_t connHandle);

 private:
  bool sendKeys(uint16_t connHandle, uint8_t mods, uint8_t usage);
  bool sendSystem(uint16_t connHandle, uint8_t bits);

  // Tracks the host's subscription to the keyboard input report (this NimBLE version has no subscriber count).
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
  NimBLECharacteristic* systemIn_ = nullptr;
};

}  // namespace mirror
#endif
