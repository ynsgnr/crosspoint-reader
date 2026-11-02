#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorActivity.h"

#include <HalDisplay.h>
#include <Logging.h>

#include "fontIds.h"
#include "templates/chat/ChatRenderer.h"  // keeps the Chat template linked; it registers itself

namespace {
constexpr char kDeviceName[] = "CrossPoint Mirror";
}  // namespace

void MirrorActivity::resetSession() {
  std::lock_guard<std::mutex> g(mutex_);
  session_ = std::make_unique<mirror::Session>(
      mirror::TemplateRegistry::instance(), [this](const std::vector<uint8_t>& frame) { ble_.send(frame); },
      kMaxPayload);
  forceFull_ = true;
  statusShown_ = false;
}

void MirrorActivity::onEnter() {
  Activity::onEnter();
  // The page turner's radio is already off: it stops before every screen change and stays off outside readers.
  resetSession();
  switch (ble_.start(kDeviceName, session_->capabilities(gfx_, kDeviceName))) {
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
  if (ble_.connected() != wasConnected_ || ble_.connectionEpoch() != seenEpoch_) {
    wasConnected_ = ble_.connected();
    seenEpoch_ = ble_.connectionEpoch();
    resetSession();  // every connection starts a new sequence space; a disconnect returns to the waiting screen
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
        line("Connected.");
        line("Open a chat app on your phone.");
      } else {
        line("Waiting for your phone");
        line(std::string("Name: ") + kDeviceName);
        const int bonds = ble_.bondCount();
        line(bonds ? std::to_string(bonds) + " paired" : "No phone paired yet");
        y += lh / 2;
        if (bondsForgotten_)
          line("Paired phones forgotten.");
        else if (bonds)
          line("Hold Confirm: forget phones");
      }
      break;
    }
  }
  y = renderer.getScreenHeight() - 2 * lh;
  line("Back: leave Mirror");
}

mirror::RefreshKind MirrorActivity::drawFrame() {
  mirror::TemplateRenderer* t = session_ ? session_->active() : nullptr;
  if (!t) {
    drawStatus();
    const bool first = !statusShown_;
    statusShown_ = true;
    forceFull_ = true;
    return first ? mirror::RefreshKind::Full : mirror::RefreshKind::Partial;
  }
  statusShown_ = false;
  forceFull_ |= session_->takeTemplateSwitched();
  forceFull_ |= session_->takeResyncHappened();
  const mirror::DirtyRects dirty = t->render(gfx_);
  const mirror::RefreshPlan plan = policy_.plan(dirty, gfx_.width(), gfx_.height(), t->refreshOverrides(), forceFull_);
  forceFull_ = false;
  return plan.kind;
}

void MirrorActivity::render(RenderLock&&) {
  mirror::RefreshKind kind;
  {
    std::lock_guard<std::mutex> lock(mutex_);  // not held during the refresh: it blocks for hundreds of ms
    kind = drawFrame();
  }
  // FAST_REFRESH only drives changed pixels, so a typed letter does not flash the page.
  if (kind == mirror::RefreshKind::Partial) renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  if (kind == mirror::RefreshKind::Full) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
#endif
