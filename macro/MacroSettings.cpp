#if defined(CROSSPOINT_MIRROR) && defined(CROSSPOINT_MACRO)
#include "MacroSettings.h"

#include <nvs.h>

namespace macro::settings {
namespace {
constexpr char kNamespace[] = "macro", kBleKey[] = "ble";
}  // namespace

bool bleEnabled() {
  nvs_handle_t h = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;  // never written: USB
  uint8_t value = 0;
  const bool ok = nvs_get_u8(h, kBleKey, &value) == ESP_OK;
  nvs_close(h);
  return ok && value != 0;
}

void setBleEnabled(bool enabled) {
  nvs_handle_t h = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  if (nvs_set_u8(h, kBleKey, enabled ? 1 : 0) == ESP_OK) nvs_commit(h);
  nvs_close(h);
}

}  // namespace macro::settings
#endif
