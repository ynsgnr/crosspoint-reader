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

// A TLS session needs total free heap and one contiguous block; free heap alone ignores fragmentation. `reserved` is
// what the caller takes out of the heap before the session starts (a buffer it reserves first): the block TLS needs
// must still be there after that.
bool heapAllowsTls(size_t reserved = 0) {
  return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP + reserved &&
         ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC + reserved;
}

// Fetches `url` into memory, at most `limit` bytes (more aborts the transfer). The buffer is reserved once, up front:
// with -fno-exceptions a failed allocation inside a growing std::string aborts the device. The caller has checked
// heapAllowsTls(limit) first.
bool fetchBytes(const char* url, size_t limit, std::string& out) {
  out.clear();
  out.reserve(limit);
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
  // A power cut during an earlier download leaves the staged file (or its .part) behind.
  Storage.remove(ota::kStagePath);
  Storage.remove((std::string(ota::kStagePath) + ".part").c_str());
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
  if (manifest_.channels.empty()) return fail("No firmware builds are published for this device yet");
  chooseChannel();
}

bool MirrorUpdateActivity::fetchManifest() {
  // The short signature first, so the manifest buffer does not sit through a second TLS handshake.
  std::string signature, json;
  if (!heapAllowsTls(ota::kMaxSignatureBytes)) {
    fail("Not enough free memory for a secure connection: go Back and try again");
    return false;
  }
  if (!fetchBytes(ota::kSignatureUrl, ota::kMaxSignatureBytes, signature)) {
    fail(HttpDownloader::lastStatus() == 404 ? "No channel list is published yet"
                                             : "Could not reach the update server: check Wi-Fi and try again");
    return false;
  }
  if (!heapAllowsTls(ota::json::kMaxDocument)) {
    fail("Not enough free memory for a secure connection: go Back and try again");
    return false;
  }
  if (!fetchBytes(ota::kManifestUrl, ota::json::kMaxDocument, json)) {
    fail("Could not reach the update server: check Wi-Fi and try again");
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
    fail(trust == ota::Trust::Stale ? "This channel list is out of date: try again later"
         : trust == ota::Trust::BadSignature
             ? "The channel list is not signed by the update key (or was being replaced): try again"
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
  // The callbacks only record the choice: show() replaces the popup's callback, so it must not run inside it.
  popup_.show(StrId::STR_UPDATE, labels, 0, [this](const int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= manifest_.channels.size()) return finish();
    pendingIndex_ = static_cast<size_t>(idx);
    pending_ = Pending::Confirm;
  });
  requestUpdate();
}

void MirrorUpdateActivity::confirmChannel(const size_t index) {
  const ota::Channel& c = manifest_.channels[index];
  const std::string headline = c.id + " (" + c.rev + "): " + clip(c.title, 60);
  const char* const options[] = {tr(STR_CANCEL), "Install"};
  {
    RenderLock lock(*this);
    state_ = State::Confirming;
  }
  // Cancel is the default: a stray Confirm press must not start an install.
  popup_.show("Install this build?", headline.c_str(), options, 2, 0, [this, index](const int idx) {
    pendingIndex_ = index;
    pending_ = idx == 1 ? Pending::Install : Pending::Choose;
  });
  requestUpdate();
}

void MirrorUpdateActivity::runPending() {
  const Pending what = pending_;
  pending_ = Pending::None;
  if (what == Pending::Choose) return chooseChannel();
  if (pendingIndex_ >= manifest_.channels.size()) return;
  if (what == Pending::Confirm) return confirmChannel(pendingIndex_);
  if (what == Pending::Install) return install(manifest_.channels[pendingIndex_]);
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
  if (!heapAllowsTls()) return fail("Not enough free memory for a download: go Back and try again");

  done_ = 0;
  total_ = c.size;
  lastPercent_ = -1;
  setState(State::Downloading);
  Storage.remove(ota::kStagePath);
  // A body longer than the signed size can only be wrong: stop it instead of filling the SD card.
  abort_ = false;
  const size_t signedSize = c.size;
  const auto err = HttpDownloader::downloadToFile(
      c.url, ota::kStagePath,
      [this, signedSize](size_t d, size_t t) {
        if (d > signedSize) abort_ = true;
        progress(d, t);
      },
      &abort_);
  if (err != HttpDownloader::OK) {
    Storage.remove(ota::kStagePath);
    return fail(abort_ ? "The download is larger than the signed channel list says"
                       : "Download failed: check Wi-Fi and the SD card, then try again");
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
                            : std::string("The image was not installed (") + firmware_flash::resultName(result) +
                                  "). The running firmware is unchanged.");
  }

  setState(State::Finished);
  delay(3000);  // the completion screen stays up long enough to read
  RenderLock lock(*this);
  state_ = State::ShuttingDown;
}

void MirrorUpdateActivity::loop() {
  if (pending_ != Pending::None) return runPending();  // before the popup checks: a chosen popup is already inactive
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
