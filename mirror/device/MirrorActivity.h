#pragma once
#if defined(CROSSPOINT_MIRROR)
// Mirror screen: BLE advertising and the connection exist only while it is open.
#include <memory>
#include <mutex>

#include "activities/Activity.h"
#include "core/RefreshPolicy.h"
#include "core/Session.h"
#include "device/GfxAdapter.h"
#include "device/MirrorBle.h"

class MirrorActivity final : public Activity {
 public:
  MirrorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Mirror", renderer, mappedInput), gfx_(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Keep the screen awake only while a phone is connected.
  bool preventAutoSleep() override { return ble_.connected(); }

 private:
  void resetSession();
  void drawStatus();
  mirror::RefreshKind drawFrame();  // draws under mutex_, returns the refresh to run

  static constexpr uint16_t kMaxPayload = 2048;

  mirror::GfxAdapter gfx_;
  mirror::MirrorBle ble_;
  mirror::RefreshPolicy policy_;
  std::unique_ptr<mirror::Session> session_;
  std::mutex mutex_;  // session_/renderer state: written by loop(), read by the render task
  enum class Startup { Ok, NoMemory, Failed } startup_ = Startup::Failed;
  uint32_t seenEpoch_ = 0;
  bool wasConnected_ = false;
  std::string drawnBand_;
  bool forceFull_ = true;
  bool statusShown_ = false;
  bool bondsForgotten_ = false;
};
#endif
