#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorActivity.h"

#include <HalDisplay.h>
#include <Logging.h>

#include "fontIds.h"
#include "templates/chat/ChatRenderer.h"  // keeps the Chat template linked; it registers itself

namespace {
constexpr char kDeviceName[] = "CrossPoint Mirror";
constexpr uint16_t kMaxPayload = 2048;  // device-side cap reported to the phone; C3 heap-friendly
}  // namespace

void MirrorActivity::resetSession() {
  std::lock_guard<std::mutex> g(mutex_);
  session_ = std::make_unique<mirror::Session>(mirror::TemplateRegistry::instance(),
                                               [this](const std::vector<uint8_t>& frame) { ble_.send(frame); });
  forceFull_ = true;
  statusShown_ = false;
}

void MirrorActivity::onEnter() {
  Activity::onEnter();
  // The page turner's radio is already off: it stops before every screen change and stays off outside readers.
  resetSession();
  mirror::Session probe(mirror::TemplateRegistry::instance(), [](const std::vector<uint8_t>&) {});
  const auto caps = probe.capabilities(gfx_, kDeviceName, kMaxPayload);
  switch (ble_.start(kDeviceName, caps)) {
    case mirror::MirrorBle::StartResult::Ok:
      startup_ = Startup::Ok;
      break;
    case mirror::MirrorBle::StartResult::NoMemory:
      startup_ = Startup::NoMemory;
      break;
    default:
      startup_ = Startup::Failed;
      break;
  }
  requestUpdate();
}

void MirrorActivity::onExit() {
  ble_.stop();
  {
    std::lock_guard<std::mutex> g(mutex_);
    session_.reset();
  }
  Activity::onExit();
}

void MirrorActivity::loop() {
  using Btn = MappedInputManager::Button;
  if (mappedInput.wasReleased(Btn::Back)) return finish();
  if (startup_ != Startup::Ok) return;

  bool redraw = ble_.takeStatusChanged();
  if (!ble_.connected() && mappedInput.wasLongPressed(Btn::Confirm, 1500)) {
    ble_.forgetBonds();
    bondsForgotten_ = true;
    redraw = true;
  }
  if (ble_.connected() != wasConnected_) {
    wasConnected_ = ble_.connected();
    if (!wasConnected_) resetSession();  // a new connection starts a new sequence space
    redraw = true;
  }

  std::vector<uint8_t> bytes;
  bool overflowed = false;
  const bool received = ble_.drain(bytes, overflowed);
  std::lock_guard<std::mutex> lock(mutex_);
  if (overflowed) session_->requestResync();
  if (received && session_->feed(bytes.data(), bytes.size())) redraw = true;

  // The event goes to the phone; the new scroll position comes back as a diff.
  if (mappedInput.wasPressed(Btn::Up) || mappedInput.wasPressed(Btn::PageBack)) session_->onButton(mirror::Button::Up);
  if (mappedInput.wasPressed(Btn::Down) || mappedInput.wasPressed(Btn::PageForward))
    session_->onButton(mirror::Button::Down);
  if (redraw) requestUpdate();
}

void MirrorActivity::drawStatus() {
  renderer.clearScreen();
  int y = renderer.getScreenHeight() / 4;
  const int lh = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  auto line = [&](const std::string& s, bool bold = false) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, s.c_str(), true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += lh;
  };
  line("Mirror", true);
  y += lh / 2;
  switch (startup_) {
    case Startup::NoMemory:
      line("Not enough free memory for Bluetooth.");
      line("Go back and try again.");
      break;
    case Startup::Failed:
      line("Bluetooth could not start.");
      break;
    case Startup::Ok: {
      const int32_t code = ble_.pairingPasskey();
      if (code >= 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "Pairing code: %06d", static_cast<int>(code));
        line(buf, true);
        line("Type it on your phone.");
      } else if (ble_.connected()) {
        line("Connected. Waiting for content...");
      } else {
        line("Waiting for your phone");
        line(std::string("Name: ") + kDeviceName);
        line(std::to_string(ble_.bondCount()) + " paired phone(s)");
        y += lh / 2;
        line(bondsForgotten_ ? "Paired phones forgotten." : "Hold Confirm: forget phones");
      }
      break;
    }
  }
  y = renderer.getScreenHeight() - 2 * lh;
  line("Back: leave Mirror");
}

void MirrorActivity::render(RenderLock&&) {
  std::lock_guard<std::mutex> g(mutex_);
  mirror::TemplateRenderer* t = session_ ? session_->active() : nullptr;
  if (!t) {
    drawStatus();
    renderer.displayBuffer(statusShown_ ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
    statusShown_ = true;
    forceFull_ = true;
    return;
  }
  statusShown_ = false;
  forceFull_ |= session_->takeTemplateSwitched();
  forceFull_ |= session_->takeResyncHappened();
  const mirror::DirtyRects dirty = t->render(gfx_);
  const mirror::RefreshPlan plan = policy_.plan(dirty, gfx_.width(), gfx_.height(), t->refreshOverrides(), forceFull_);
  forceFull_ = false;
  switch (plan.kind) {
    case mirror::RefreshKind::None:
      break;
    // FAST_REFRESH only drives changed pixels, so a typed letter does not flash the page.
    case mirror::RefreshKind::Partial:
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      break;
    case mirror::RefreshKind::Full:
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      break;
  }
}
#endif
