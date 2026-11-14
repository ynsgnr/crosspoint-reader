#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorSettings.h"

#include <nvs.h>

namespace mirror::settings {
namespace {

struct Nvs {
  nvs_handle_t h = 0;
  bool ok;
  explicit Nvs(nvs_open_mode_t mode) : ok(nvs_open("mirror", mode, &h) == ESP_OK) {}
  ~Nvs() {
    if (ok) nvs_close(h);
  }
};

}  // namespace

bool securityEpochStale(uint8_t epoch) {
  nvs_handle_t h = 0;
  const esp_err_t opened = nvs_open("mirror", NVS_READONLY, &h);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;  // never recorded
  if (opened != ESP_OK) return false;                // unreadable: do not touch the bonds
  uint8_t stored = 0;
  const esp_err_t got = nvs_get_u8(h, "sec_epoch", &stored);
  nvs_close(h);
  if (got == ESP_ERR_NVS_NOT_FOUND) return true;
  return got == ESP_OK && stored != epoch;
}

void commitSecurityEpoch(uint8_t epoch) {
  Nvs n(NVS_READWRITE);
  if (n.ok) nvs_set_u8(n.h, "sec_epoch", epoch), nvs_commit(n.h);
}

uint32_t otaLastIssued() {
  nvs_handle_t h = 0;
  if (nvs_open("mirror", NVS_READONLY, &h) != ESP_OK) return 0;
  uint32_t v = 0;
  if (nvs_get_u32(h, "ota_issued", &v) != ESP_OK) v = 0;
  nvs_close(h);
  return v;
}

void commitOtaIssued(uint32_t issued) {
  if (issued <= otaLastIssued()) return;  // only ever forward
  Nvs n(NVS_READWRITE);
  if (n.ok) nvs_set_u32(n.h, "ota_issued", issued), nvs_commit(n.h);
}

}  // namespace mirror::settings
#endif
