#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorHid.h"

#include <NimBLEDevice.h>

namespace mirror {
namespace {

constexpr uint16_t kUuidHidService = 0x1812, kUuidBatteryService = 0x180F, kUuidDeviceInfoService = 0x180A;
constexpr uint8_t kReportKeyboard = 1, kReportConsumer = 2, kReportSystem = 3;

// Keyboard (report 1: modifiers, reserved, 6 keys in; LEDs out), consumer control (report 2: one 16-bit usage),
// system control (report 3: power, sleep, wake bits).
const uint8_t kReportMap[] = {
    0x05,
    0x01,
    0x09,
    0x06,
    0xA1,
    0x01,
    0x85,
    kReportKeyboard,
    0x05,
    0x07,
    0x19,
    0xE0,
    0x29,
    0xE7,
    0x15,
    0x00,
    0x25,
    0x01,
    0x75,
    0x01,
    0x95,
    0x08,
    0x81,
    0x02,
    0x95,
    0x01,
    0x75,
    0x08,
    0x81,
    0x01,
    0x95,
    0x05,
    0x75,
    0x01,
    0x05,
    0x08,
    0x19,
    0x01,
    0x29,
    0x05,
    0x91,
    0x02,
    0x95,
    0x01,
    0x75,
    0x03,
    0x91,
    0x01,
    0x95,
    0x06,
    0x75,
    0x08,
    0x15,
    0x00,
    0x25,
    0x65,
    0x05,
    0x07,
    0x19,
    0x00,
    0x29,
    0x65,
    0x81,
    0x00,
    0xC0,
    0x05,
    0x0C,
    0x09,
    0x01,
    0xA1,
    0x01,
    0x85,
    kReportConsumer,
    0x15,
    0x00,
    0x26,
    0xFF,
    0x03,
    0x19,
    0x00,
    0x2A,
    0xFF,
    0x03,
    0x75,
    0x10,
    0x95,
    0x01,
    0x81,
    0x00,
    0xC0,
    0x05,
    0x01,
    0x09,
    0x80,
    0xA1,
    0x01,
    0x85,
    kReportSystem,
    0x15,
    0x00,
    0x25,
    0x01,
    0x09,
    0x81,
    0x09,
    0x82,
    0x09,
    0x83,
    0x75,
    0x01,
    0x95,
    0x03,
    0x81,
    0x06,
    0x75,
    0x05,
    0x95,
    0x01,
    0x81,
    0x03,
    0xC0,
};

// Everything needs an encrypted, authenticated link; the subscription (CCCD) is covered by MirrorBle's global setting.
constexpr uint32_t kRead = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN;
constexpr uint32_t kWriteNr = NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN;

NimBLECharacteristic* addReport(NimBLEService* hid, uint8_t id, uint8_t type, uint32_t props, size_t len) {
  NimBLECharacteristic* c = hid->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0x2A4D)), props, len);
  NimBLEDescriptor* ref = c->createDescriptor(NimBLEUUID(static_cast<uint16_t>(0x2908)), kRead, 2);
  const uint8_t value[2] = {id, type};  // report id, 1 = input / 2 = output
  ref->setValue(value, sizeof value);
  return c;
}

}  // namespace

void MirrorHid::create(NimBLEServer* server) {
  NimBLEService* info = server->createService(NimBLEUUID(kUuidDeviceInfoService));
  const uint8_t pnp[] = {0x02, 0x34, 0x12, 0x01, 0x00, 0x00, 0x01};  // USB-IF source, placeholder ids, version 1.0
  info->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0x2A50)), kRead, sizeof pnp)->setValue(pnp, sizeof pnp);

  NimBLEService* hid = server->createService(NimBLEUUID(kUuidHidService));
  const uint8_t hidInfo[] = {0x11, 0x01, 0x00, 0x02};  // HID 1.11, no country, normally connectable
  hid->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0x2A4A)), kRead, sizeof hidInfo)
      ->setValue(hidInfo, sizeof hidInfo);
  hid->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0x2A4B)), kRead, sizeof kReportMap)
      ->setValue(kReportMap, sizeof kReportMap);
  hid->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0x2A4C)), kWriteNr, 1);  // control point (suspend/resume)
  NimBLECharacteristic* mode =
      hid->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0x2A4E)), kRead | kWriteNr, 1);
  const uint8_t reportProtocol = 1;
  mode->setValue(&reportProtocol, 1);

  keyboardIn_ = addReport(hid, kReportKeyboard, 1, kRead | NIMBLE_PROPERTY::NOTIFY, 8);
  keyboardIn_->setCallbacks(&subscription_);
  consumerIn_ = addReport(hid, kReportConsumer, 1, kRead | NIMBLE_PROPERTY::NOTIFY, 2);
  systemIn_ = addReport(hid, kReportSystem, 1, kRead | NIMBLE_PROPERTY::NOTIFY, 1);
  addReport(hid, kReportKeyboard, 2,
            kRead | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC |
                NIMBLE_PROPERTY::WRITE_AUTHEN,
            1);  // keyboard LEDs: accepted and ignored

  NimBLEService* battery = server->createService(NimBLEUUID(kUuidBatteryService));
  const uint8_t level = 100;  // a fixed value; the e-ink device's battery is not this service's business
  battery->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0x2A19)), kRead, 1)->setValue(&level, 1);
}

bool MirrorHid::sendKeys(uint16_t connHandle, uint8_t mods, uint8_t usage) {
  const uint8_t report[8] = {mods, 0, usage, 0, 0, 0, 0, 0};
  return keyboardIn_->notify(report, sizeof report, connHandle);
}

bool MirrorHid::sendSystem(uint16_t connHandle, uint8_t bits) { return systemIn_->notify(&bits, 1, connHandle); }

bool MirrorHid::tap(uint16_t connHandle, uint8_t mods, uint8_t usage) {
  if (!ready()) return false;
  const bool down = sendKeys(connHandle, mods, usage);
  vTaskDelay(pdMS_TO_TICKS(15));  // hosts drop a press and release that arrive in the same connection event
  const bool up = sendKeys(connHandle, 0, 0);  // always release, even if the press failed
  vTaskDelay(pdMS_TO_TICKS(15));
  return down && up;
}

bool MirrorHid::tapConsumer(uint16_t connHandle, uint16_t usage) {
  if (!ready()) return false;
  const uint8_t pressed[2] = {static_cast<uint8_t>(usage & 0xFF), static_cast<uint8_t>(usage >> 8)};
  const uint8_t released[2] = {0, 0};
  const bool down = consumerIn_->notify(pressed, sizeof pressed, connHandle);
  vTaskDelay(pdMS_TO_TICKS(15));
  const bool up = consumerIn_->notify(released, sizeof released, connHandle);  // always release
  vTaskDelay(pdMS_TO_TICKS(15));
  return down && up;
}

bool MirrorHid::wake(uint16_t connHandle) {
  if (!ready()) return false;
  const bool shift = tap(connHandle, 0x02, 0);     // modifier only: a plain "any key" event
  const bool down = sendSystem(connHandle, 0x04);  // bit 2 = System Wake Up
  vTaskDelay(pdMS_TO_TICKS(15));
  const bool up = sendSystem(connHandle, 0);
  return (shift || down) && up;
}

}  // namespace mirror
#endif
