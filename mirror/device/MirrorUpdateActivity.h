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
  bool preventAutoSleep() override { return busy(); }
  bool skipLoopDelay() override { return busy(); }  // no power saving while a transfer runs, but in the menus

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

  // A step that blocks the loop and must not be interrupted by sleep: everything except the menus and the end screens.
  bool busy() const {
    return state_ == State::Fetching || state_ == State::Downloading || state_ == State::Checking ||
           state_ == State::Flashing;
  }
  // What a popup choice asks for. Run from loop(), never inside the popup's own callback (show() replaces it).
  enum class Pending { None, Choose, Confirm, Install };

  void runPending();
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
  Pending pending_ = Pending::None;
  size_t pendingIndex_ = 0;
  bool abort_ = false;  // set when the download outgrows the signed size
};
#endif
