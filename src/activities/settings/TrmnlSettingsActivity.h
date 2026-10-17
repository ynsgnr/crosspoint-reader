#pragma once

#include "activities/UiListActivity.h"

// Edit screen for the TRMNL / LaraPaper server, access token and model id.
class TrmnlSettingsActivity final : public UiListActivity {
 public:
  explicit TrmnlSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr int FIELD_COUNT = 3;
  freeink::ui::ListItem rows[FIELD_COUNT]{};

  int listCount() const override { return FIELD_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
};
