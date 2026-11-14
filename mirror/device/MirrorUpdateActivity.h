#pragma once
#if defined(CROSSPOINT_MIRROR)
// Firmware channels: installs a build CI published (the develop branch or an open pull request), after checking it.
//   Wi-Fi -> signed manifest (signature first, then the policy of ota/OtaManifest.h) -> choose a channel -> confirm ->
//   download to the SD card -> SHA-256 against the manifest -> the firmware flasher's own image checks and flash.
#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "ota/OtaManifest.h"

class MirrorUpdateActivity final : public Activity {
 public:
  MirrorUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MirrorUpdate", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    return state_ != State::Choosing && state_ != State::Confirming && state_ != State::Failed;
  }
  bool skipLoopDelay() override { return true; }  // no power saving while a transfer runs

 private:
  enum class State {
    Wifi,
    Fetching,
    Choosing,
    Confirming,
    Downloading,
    Checking,
    Flashing,
    Finished,
    Failed,
    ShuttingDown
  };

  void onWifiSelectionComplete(bool success);
  bool fetchManifest();  // false: failed_ says why
  void chooseChannel();
  void confirmChannel(size_t index);
  void install(const mirror::ota::Channel& channel);
  void fail(std::string why);
  void setState(State s);
  void progress(size_t done, size_t total);

  State state_ = State::Wifi;
  mirror::ota::Manifest manifest_;
  std::string failure_;
  size_t done_ = 0, total_ = 0;
  int lastPercent_ = -1;
  OptionPopup popup_;
};
#endif
