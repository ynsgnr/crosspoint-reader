#if defined(CROSSPOINT_MIRROR)
#include "device/MoreMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "device/MirrorUpdateActivity.h"

namespace fui = freeink::ui;

MoreMenuActivity::MoreMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int libraryEntry,
                                   bool opdsServers)
    : UiListActivity("More", renderer, mappedInput), plugins_(libraryEntry == 2), opdsServers_(opdsServers) {
  auto add = [this](Row row, const char* label, UIIcon icon) {
    fui::ListItem item;
    item.label = label;
    item.icon = listIconFor(icon);
    item.actionValue = static_cast<int16_t>(rowCount_);
    rows_[rowCount_] = row;
    items_[rowCount_++] = item;
  };
  add(Row::FileBrowser, tr(STR_BROWSE_FILES), UIIcon::Folder);
  add(Row::FileTransfer, tr(STR_FILE_TRANSFER), UIIcon::Transfer);
  if (libraryEntry != 0) add(Row::Library, plugins_ ? tr(STR_PLUGINS) : tr(STR_OPDS_BROWSER), UIIcon::Plugins);
  add(Row::Settings, tr(STR_SETTINGS_TITLE), UIIcon::Settings);
  add(Row::Firmware, "Firmware channels", UIIcon::Transfer);
}

void MoreMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = items_;
  props.count = static_cast<uint16_t>(rowCount_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void MoreMenuActivity::activateIndex(int index) {
  app.clearTapFlash();  // a lingering flash would gray an unrelated element on the next screen
  nav.selected = index;
  switch (rows_[index]) {
    case Row::FileBrowser:
      activityManager.goToFileBrowser();
      break;
    case Row::FileTransfer:
      activityManager.goToFileTransfer();
      break;
    case Row::Library:
      plugins_ ? activityManager.goToPlugins(opdsServers_) : activityManager.goToBrowser();
      break;
    case Row::Settings:
      activityManager.goToSettings();
      break;
    case Row::Firmware:
      activityManager.replaceActivity(std::make_unique<MirrorUpdateActivity>(renderer, mappedInput));
      break;
  }
}
#endif
