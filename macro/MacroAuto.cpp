#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
#include "MacroAuto.h"

#if MACRO_HAS_USB
#include <Arduino.h>
#include <driver/usb_serial_jtag.h>
#include <esp_attr.h>
#include <nvs.h>

namespace macro::autoopen {
namespace {
constexpr char kNamespace[] = "macro", kAutoKey[] = "auto";
constexpr uint32_t kHostSettleMs = 3000;         // a PC must be seen this long before Macro opens
constexpr uint32_t kUnplugMs = 2000;             // a cable out this long clears the suppress latch
constexpr uint32_t kSuppressMagic = 0x4D414353;  // 'MACS'

// Survives the restart that closing Macro causes; garbage after a power cycle, hence the magic.
RTC_NOINIT_ATTR uint32_t suppressed;
int gEnabled = -1;  // the setting, -1 until read
}  // namespace

bool enabled() {
  if (gEnabled < 0) {  // read the flash once per boot
    nvs_handle_t h = 0;
    uint8_t value = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &h) == ESP_OK) {
      if (nvs_get_u8(h, kAutoKey, &value) != ESP_OK) value = 0;
      nvs_close(h);
    }
    gEnabled = value != 0;
  }
  return gEnabled != 0;
}

void setEnabled(bool on) {
  gEnabled = on ? 1 : 0;
  nvs_handle_t h = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  if (nvs_set_u8(h, kAutoKey, on ? 1 : 0) == ESP_OK) nvs_commit(h);
  nvs_close(h);
}

bool shouldOpen(bool userActive) {
  static bool userSeen = false;  // any input since boot cancels the auto-open for good
  static uint32_t hostSince = 0, goneSince = 0;
  if (!enabled()) return false;
  if (userActive) userSeen = true;

  const uint32_t now = millis();
  // True while the PC sends USB start-of-frame packets; a charger or power bank never does.
  if (!usb_serial_jtag_is_connected()) {
    hostSince = 0;
    if (goneSince == 0) goneSince = now;
    if (now - goneSince >= kUnplugMs) {
      suppressed = 0;
      userSeen = false;  // a new plug-in starts over
    }
    return false;
  }
  goneSince = 0;
  if (hostSince == 0) hostSince = now;
  if (userSeen || suppressed == kSuppressMagic) return false;
  return now - hostSince >= kHostSettleMs;
}

void suppressUntilUnplug() { suppressed = kSuppressMagic; }

}  // namespace macro::autoopen
#endif
#endif
