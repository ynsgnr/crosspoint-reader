#include "TrmnlSettingsActivity.h"

#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
struct Field {
  StrId label;
  char* value;
  size_t capacity;
  InputType inputType;
};

// Built on first use: SETTINGS must exist before its field addresses are taken.
const Field* fields() {
  static const Field fields[] = {
      {StrId::STR_TRMNL_SERVER, SETTINGS.trmnlServer, sizeof(SETTINGS.trmnlServer), InputType::Url},
      {StrId::STR_TRMNL_TOKEN, SETTINGS.trmnlToken, sizeof(SETTINGS.trmnlToken), InputType::Text},
      {StrId::STR_TRMNL_MODEL_ID, SETTINGS.trmnlModelId, sizeof(SETTINGS.trmnlModelId), InputType::Text},
  };
  return fields;
}
constexpr int TOKEN_FIELD = 1;
}  // namespace

TrmnlSettingsActivity::TrmnlSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("TrmnlSettings", renderer, mappedInput) {
  for (int i = 0; i < FIELD_COUNT; i++) {
    rows[i].label = I18N.get(fields()[i].label);
    rows[i].actionValue = static_cast<int16_t>(i);
  }
}

void TrmnlSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  const Field& field = fields()[index];
  auto handler = [this, index](const ActivityResult& result) {
    if (!result.isCancelled) {
      const Field& edited = fields()[index];
      strlcpy(edited.value, std::get<KeyboardResult>(result.data).text.c_str(), edited.capacity);
      SETTINGS.saveToFile();
      requestUpdate();
    }
  };
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(field.label),
                                                                 field.value, field.capacity - 1, field.inputType),
                         handler);
}

void TrmnlSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});

  for (int i = 0; i < FIELD_COUNT; i++) {
    const char* value = fields()[i].value;
    if (i == TOKEN_FIELD) {
      rows[i].value = value[0] ? "******" : tr(STR_TRMNL_AUTO_JOIN);
    } else {
      rows[i].value = value[0] ? value : tr(STR_NOT_SET);
    }
  }

  fui::ListProps props;
  props.items = rows;
  props.count = FIELD_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

const char* TrmnlSettingsActivity::headerTitle() const { return tr(STR_TRMNL); }
