// BLE page-turner overlay for MappedInputManager (X4 Pro stack, see BleInput.h). Kept apart from
// MappedInputManager.cpp so the host-side input tests do not need the BLE host.
#include <BleKeyboardHost.h>

#include "BleInput.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"

void MappedInputManager::setBleCaptureMode(const bool on) {
  bleCaptureMode = on;
  bleHasCaptured = false;
  if (on) {
    // Clear any stale overlay so a held remote key doesn't leak into the UI.
    for (uint8_t i = 0; i < kButtonCount; i++) {
      blePressEdge[i] = false;
      bleReleaseEdge[i] = false;
    }
  }
}

bool MappedInputManager::takeCapturedBleKey(uint8_t& kind, uint8_t& value) {
  if (!bleHasCaptured) return false;
  kind = bleCapturedKind;
  value = bleCapturedValue;
  bleHasCaptured = false;
  return true;
}

void MappedInputManager::pollBle() {
  bleActivityThisFrame = false;
  // Age last frame's press edges into this frame's release edges (the FreeInk host
  // surfaces presses + synthetic repeats but never releases), then clear presses. A
  // pending release also counts as BLE activity this frame so getHeldTime() reports
  // zero on the release frame too (page-turn handlers often fire on release).
  for (uint8_t i = 0; i < kButtonCount; i++) {
    bleReleaseEdge[i] = blePressEdge[i];
    blePressEdge[i] = false;
    if (bleReleaseEdge[i]) bleActivityThisFrame = true;
  }

  freeink::KeyEvent ev;
  while (BleHid.popKey(ev)) {
    uint8_t kind = 0xFF;
    uint8_t value = 0;
    if (!bleinput::encodeKey(ev, kind, value)) continue;

    if (bleCaptureMode) {
      bleCapturedKind = kind;
      bleCapturedValue = value;
      bleHasCaptured = true;
      continue;
    }

    // Resolve the key identity against the persisted mapping table.
    for (const auto& e : SETTINGS.bleKeyMap) {
      if (e.button == 0xFF || e.keyKind != kind || e.keyValue != value) continue;
      if (e.button < kButtonCount) {
        blePressEdge[e.button] = true;
        bleActivityThisFrame = true;
      }
      break;
    }
  }
}
