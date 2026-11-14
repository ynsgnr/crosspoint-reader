#if defined(CROSSPOINT_MIRROR)
#include "device/OtaBoot.h"

#include <Logging.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <nvs.h>

#include <cstring>

#include "network/FirmwareFlasher.h"
#include "network/OtaBootSwitch.h"
#include "ota/BootGuard.h"
#if __has_include("device/BuildInfo.h")
#include "device/BuildInfo.h"  // MIRROR_GIT_REV, written by mirror/build.py
#endif
#ifndef MIRROR_GIT_REV
#define MIRROR_GIT_REV "unknown"
#endif

namespace mirror::ota {
namespace {

constexpr char kNamespace[] = "mirror";
constexpr char kKey[] = "ota_guard";

bool load(BootGuard& g) {
  nvs_handle_t h = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
  uint8_t blob[kGuardBlobSize];
  size_t len = sizeof(blob);
  const esp_err_t got = nvs_get_blob(h, kKey, blob, &len);
  nvs_close(h);
  return got == ESP_OK && decodeGuard(blob, len, g);
}

bool save(const BootGuard& g) {
  nvs_handle_t h = 0;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  uint8_t blob[kGuardBlobSize];
  encodeGuard(g, blob);
  esp_err_t err = nvs_set_blob(h, kKey, blob, sizeof(blob));
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

// The slot must hold something that looks like an image for this chip: an erased or foreign slot is never switched to.
bool plausibleImage(const esp_partition_t* p) {
  uint8_t head[14];
  if (!p || esp_partition_read(p, 0, head, sizeof(head)) != ESP_OK) return false;
  uint16_t chip;
  std::memcpy(&chip, head + 12, sizeof(chip));
  const uint16_t running = firmware_flash::runningPartitionChipId();
  return head[0] == 0xE9 && (running == 0xFFFF || chip == running);
}

}  // namespace

bool armBootGuard(const std::string& rev) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return false;
  BootGuard g;
  load(g);  // an unreadable old record is simply replaced
  armGuard(g, static_cast<uint8_t>(running->subtype), rev);
  return save(g);
}

void disarmBootGuard() {
  BootGuard g;
  if (!load(g)) return;
  disarmGuard(g);
  save(g);
}

void bootGuardAtStart() {
  BootGuard g;
  if (!load(g)) return;  // nothing recorded, or unreadable: never act on a record that cannot be trusted
  const BootAction action = onBootGuard(g, MIRROR_GIT_REV);
  if (action == BootAction::Nothing) return;
  if (action != BootAction::Rollback) {
    save(g);
    LOG_INF("OTA", "build on trial: boot %u of %u", static_cast<unsigned>(g.tries),
            static_cast<unsigned>(kMaxBootTries));
    return;
  }

  // Out of tries. Go back to the old image if it is still there; if not, leave the record cleared so this does not
  // repeat.
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* prev =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, static_cast<esp_partition_subtype_t>(g.prevSlot), nullptr);
  const bool canGoBack = prev && running && prev->address != running->address && plausibleImage(prev);
  g.rolledBack = canGoBack;
  LOG_ERR("OTA", "build %s did not confirm in %u boots: %s", MIRROR_GIT_REV, static_cast<unsigned>(kMaxBootTries),
          canGoBack ? "rolling back" : "no usable previous image");
  if (canGoBack && ota_boot::switchTo(prev)) {
    save(g);
    esp_restart();
  }
  g.rolledBack = false;
  save(g);
}

void bootGuardTick(uint32_t uptimeMs) {
  static bool settled = false;
  if (settled || uptimeMs < kConfirmAfterMs) return;
  settled = true;  // one look: a build that is not on trial has nothing to confirm
  BootGuard g;
  if (!load(g) || !guardConfirmDue(g, uptimeMs)) return;
  confirmGuard(g);
  if (save(g)) LOG_INF("OTA", "build %s confirmed", MIRROR_GIT_REV);
}

bool rolledBackNotice() {
  BootGuard g;
  return load(g) && g.rolledBack;
}

}  // namespace mirror::ota
#endif
