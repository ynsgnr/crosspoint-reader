#if defined(CROSSPOINT_MIRROR)
// Sources: ota/ (portable manifest rules, host-tested), device/OtaVerify (signature, SHA-256), network/FirmwareFlasher
// (image validation and flash, shared with the SD-card update).
// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's ip4_addr.h unless seen first (see OtaUpdater.cpp).
#include "network/HttpDownloader.h"
#include "device/MirrorUpdateActivity.h"
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
// clang-format on

#include <algorithm>
#include <vector>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "device/MirrorSettings.h"
#include "device/OtaVerify.h"
#include "fontIds.h"
#include "network/FirmwareBoardTag.h"
#include "network/FirmwareFlasher.h"
#include "ota/MiniJson.h"
#include "ota/OtaConfig.h"
#if __has_include("device/BuildInfo.h")
#include "device/BuildInfo.h"  // MIRROR_GIT_REV, written by mirror/build.py
#endif
#ifndef MIRROR_GIT_REV
#define MIRROR_GIT_REV "unknown"
#endif

namespace ota = mirror::ota;

namespace {
std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "~"; }

// Fetches `url` into memory, at most `limit` bytes (more aborts the transfer).
bool fetchBytes(const char* url, size_t limit, std::string& out) {
  out.clear();
  bool tooBig = false;
  const bool ok = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, size_t len) {
    if (out.size() + len > limit) {
      tooBig = true;
      return false;
    }
    out.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  return ok && !tooBig;
}
}  // namespace

void MirrorUpdateActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void MirrorUpdateActivity::onExit() {
  Activity::onExit();
  // Like the stock updater: restart to hand the Wi-Fi and TLS heap back instead of living with its fragmentation.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void MirrorUpdateActivity::setState(State s) {
  {
    RenderLock lock(*this);
    state_ = s;
  }
  requestUpdateAndWait();
}

void MirrorUpdateActivity::fail(std::string why) {
  LOG_ERR("OTA", "failed: %s", why.c_str());
  RenderLock lock(*this);
  failure_ = std::move(why);
  state_ = State::Failed;
  requestUpdate();
}

void MirrorUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) return finish();
  setState(State::Fetching);
  if (!fetchManifest()) return;
  if (manifest_.channels.empty()) return fail("No firmware builds are published for this device");
  chooseChannel();
}

bool MirrorUpdateActivity::fetchManifest() {
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP) {
    fail("Not enough free memory for a secure connection");
    return false;
  }
  std::string json, signature;
  if (!fetchBytes(ota::kManifestUrl, ota::json::kMaxDocument, json) ||
      !fetchBytes(ota::kSignatureUrl, ota::kMaxSignatureBytes, signature)) {
    fail(HttpDownloader::lastStatus() == 404 ? "No channel list is published yet"
                                             : "Could not reach the update server");
    return false;
  }
  ota::Policy policy;
  policy.board.assign(board_tag::boardName(), board_tag::boardNameLen());
  policy.urlPrefix = ota::kReleaseBase;
  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  policy.maxImageBytes = dest ? dest->size : 0;
  policy.lastIssued = mirror::settings::otaLastIssued();
  const ota::EcdsaVerifier verifier;
  const ota::Trust trust = ota::openManifest(json, reinterpret_cast<const uint8_t*>(signature.data()), signature.size(),
                                             verifier, policy, manifest_);
  if (trust != ota::Trust::Ok) {
    fail(trust == ota::Trust::Stale          ? "The channel list is older than one already seen"
         : trust == ota::Trust::BadSignature ? "The channel list is not signed by the update key"
                                             : "The channel list is not valid");
    return false;
  }
  mirror::settings::commitOtaIssued(manifest_.issued);
  return true;
}

void MirrorUpdateActivity::chooseChannel() {
  std::vector<std::string> labels;
  for (const ota::Channel& c : manifest_.channels) {
    const bool running = !ota::differsFromRunning(c, MIRROR_GIT_REV);
    labels.push_back(c.id + " " + c.rev + (running ? " (running)" : "") + ": " + clip(c.title, 28));
  }
  labels.push_back(tr(STR_CANCEL));
  {
    RenderLock lock(*this);
    state_ = State::Choosing;
  }
  popup_.show(StrId::STR_UPDATE, labels, 0, [this](const int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= manifest_.channels.size()) return finish();
    confirmChannel(static_cast<size_t>(idx));
  });
  requestUpdate();
}

void MirrorUpdateActivity::confirmChannel(const size_t index) {
  const ota::Channel& c = manifest_.channels[index];
  const std::string headline = c.id + " (" + c.rev + "): " + clip(c.title, 60);
  static const char* const options[] = {"Cancel", "Install"};
  {
    RenderLock lock(*this);
    state_ = State::Confirming;
  }
  // Cancel is the default: a stray Confirm press must not start an install.
  popup_.show("Install this build?", headline.c_str(), options, 2, 0, [this, index](const int idx) {
    if (idx == 1) return install(manifest_.channels[index]);
    chooseChannel();
  });
  requestUpdate();
}

void MirrorUpdateActivity::progress(const size_t done, const size_t total) {
  done_ = done;
  total_ = total;
  const int pct = total ? static_cast<int>(static_cast<uint64_t>(done) * 100 / total) : 0;
  if (pct == lastPercent_) return;  // the render task contends with TLS for heap: one redraw per percent
  lastPercent_ = pct;
  requestUpdate(true);
}

void MirrorUpdateActivity::install(const ota::Channel& c) {
  if (powerManager.getBatteryPercentage() < ota::kMinBatteryPercent)
    return fail("Battery is below " + std::to_string(ota::kMinBatteryPercent) + "%: charge it first");
  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest || c.size > dest->size) return fail("The image does not fit the update partition");
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP) return fail("Not enough free memory for a download");

  done_ = 0;
  total_ = c.size;
  lastPercent_ = -1;
  setState(State::Downloading);
  Storage.remove(ota::kStagePath);
  const auto err =
      HttpDownloader::downloadToFile(c.url, ota::kStagePath, [this](size_t d, size_t t) { progress(d, t); });
  if (err != HttpDownloader::OK) {
    Storage.remove(ota::kStagePath);
    return fail("Download failed");
  }

  setState(State::Checking);
  std::string hex;
  size_t size = 0;
  if (!ota::sha256OfFile(ota::kStagePath, hex, size) || size != c.size || hex != c.sha256) {
    Storage.remove(ota::kStagePath);
    return fail("The download does not match the signed channel list");
  }

  // The flasher validates the whole image (header, checksum, SHA-256 trailer, chip, board tag) before it erases
  // anything, and only then switches the boot partition.
  done_ = 0;
  lastPercent_ = -1;
  setState(State::Flashing);
  const auto result = firmware_flash::flashFromSdPath(
      ota::kStagePath,
      +[](size_t written, size_t total, void* ctx) {
        static_cast<MirrorUpdateActivity*>(ctx)->progress(written, total);
      },
      this);
  Storage.remove(ota::kStagePath);
  if (result != firmware_flash::Result::OK) {
    const bool wrongDevice =
        result == firmware_flash::Result::BAD_CHIP || result == firmware_flash::Result::WRONG_BOARD;
    return fail(wrongDevice ? tr(STR_FIRMWARE_WRONG_DEVICE)
                            : std::string("The image was refused: ") + firmware_flash::resultName(result));
  }

  setState(State::Finished);
  delay(3000);  // the completion screen stays up long enough to read
  RenderLock lock(*this);
  state_ = State::ShuttingDown;
}

void MirrorUpdateActivity::loop() {
  if (state_ == State::Choosing || state_ == State::Confirming) {
    if (popup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    finish();  // dismissed without a choice
    return;
  }
  if (state_ == State::Failed) {
    int x = 0, y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) finish();
    return;
  }
  if (state_ == State::ShuttingDown) ESP.restart();
}

void MirrorUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const int top = (h - line) / 2;
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, "Firmware channels");

  switch (state_) {
    case State::Wifi:
    case State::Fetching:
      renderer.drawCenteredText(UI_10_FONT_ID, top, "Checking for builds...");
      break;
    case State::Choosing:
    case State::Confirming: {
      const int infoTop = h / 6;
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop,
                        (std::string("Running: ") + CROSSPOINT_VERSION + " " + MIRROR_GIT_REV).c_str());
      if (popup_.processRender(renderer, mappedInput)) return;
      break;
    }
    case State::Downloading:
    case State::Flashing: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, state_ == State::Downloading ? "Downloading..." : "Installing...");
      const int y = top + line + metrics.verticalSpacing;
      const int pct = total_ ? static_cast<int>(static_cast<uint64_t>(done_) * 100 / total_) : 0;
      GUI.drawProgressBar(
          renderer, Rect{metrics.contentSidePadding, y, w - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          pct, 100);
      break;
    }
    case State::Checking:
      renderer.drawCenteredText(UI_10_FONT_ID, top, "Checking the download...");
      break;
    case State::Failed: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
      const int y = top + line + metrics.verticalSpacing;
      UITheme::drawCenteredWrappedText(
          renderer, Rect{metrics.contentSidePadding, y, w - metrics.contentSidePadding * 2, h - y}, UI_10_FONT_ID,
          failure_.c_str(), 4, true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case State::Finished:
    case State::ShuttingDown: {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
      const int y = top + line + metrics.verticalSpacing;
      UITheme::drawCenteredWrappedText(
          renderer, Rect{metrics.contentSidePadding, y, w - metrics.contentSidePadding * 2, h - y}, UI_10_FONT_ID,
          tr(STR_POWER_ON_HINT), 3, true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
      break;
    }
  }
  renderer.displayBuffer();
}
#endif
