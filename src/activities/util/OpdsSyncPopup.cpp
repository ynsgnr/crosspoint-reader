#include "OpdsSyncPopup.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <cstdio>
#include <utility>

#include "activities/ActivityResult.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "network/OpdsAutoSync.h"

namespace {

void syncWithPopups(GfxRenderer& renderer, void (*restartAfterSync)()) {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  const opdssync::Summary summary = opdssync::syncNow();

  char message[64];
  if (summary.wifiFailed) {
    snprintf(message, sizeof(message), "%s", tr(STR_SYNC_NO_WIFI));
  } else {
    snprintf(message, sizeof(message), "%s: %u", tr(STR_SYNC_RESULT), summary.downloaded);
  }
  GUI.drawPopup(renderer, message);
  delay(2500);
  restartAfterSync();
}

}  // namespace

void startOpdsSync(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput, void (*restartAfterSync)()) {
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) return;
  host.startActivityForResult(std::move(wifi), [&renderer, restartAfterSync](const ActivityResult& result) {
    if (!result.isCancelled) syncWithPopups(renderer, restartAfterSync);
  });
}
