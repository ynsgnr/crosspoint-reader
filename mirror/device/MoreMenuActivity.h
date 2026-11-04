#pragma once
#if defined(CROSSPOINT_MIRROR)
// "More...": the Home entries that no longer fit once Mirror is in the list (File Browser, File Transfer, OPDS or
// Plugins, Settings). Each row opens its usual screen; Back returns to Home.
#include "activities/UiListActivity.h"

class MoreMenuActivity final : public UiListActivity {
 public:
  // `libraryEntry`: 0 none, 1 OPDS browser, 2 plugins (what Home shows in its library slot).
  MoreMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int libraryEntry, bool opdsServers);

 private:
  enum class Row { FileBrowser, FileTransfer, Library, Settings };

  int listCount() const override { return rowCount_; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { activityManager.goHome(); }
  const char* headerTitle() const override { return "More"; }

  static constexpr int kMaxRows = 4;
  const bool plugins_;
  const bool opdsServers_;
  int rowCount_ = 0;
  Row rows_[kMaxRows]{};
  freeink::ui::ListItem items_[kMaxRows]{};
};
#endif
