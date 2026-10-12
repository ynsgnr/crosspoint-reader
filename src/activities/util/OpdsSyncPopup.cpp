#include "OpdsSyncPopup.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "components/UITheme.h"
#include "network/OpdsAutoSync.h"

void runOpdsSyncWithPopups(GfxRenderer& renderer, void (*restartAfterSync)()) {
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
