#pragma once

class GfxRenderer;

// Blocking manual OPDS sync with popups for progress and result. Reboots through
// `restartAfterSync` afterwards: TLS leaves the heap fragmented.
void runOpdsSyncWithPopups(GfxRenderer& renderer, void (*restartAfterSync)());
