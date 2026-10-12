#pragma once

class Activity;
class GfxRenderer;
class MappedInputManager;

// Manual OPDS sync: connects through the Wi-Fi selection screen like the OPDS browser,
// then syncs with popups. Reboots through `restartAfterSync` afterwards: TLS leaves the heap fragmented.
void startOpdsSync(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput, void (*restartAfterSync)());
