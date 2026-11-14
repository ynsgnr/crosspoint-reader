#pragma once
// One panel frame: a status band on top, the template below it. Shared by the device screen and the simulator.
#include <optional>
#include <string>

#include "core/Gfx.h"
#include "core/TemplateRenderer.h"
#include "protocol_gen.h"

namespace mirror {

int statusBandHeight(const Gfx& gfx);

bool bandDarkMode(const std::optional<gen::PhoneStatus>& phone);

// Draws the template under the band, then the band: the phone's dark mode as a dot on the left (filled = on, hollow =
// off; tapping the band toggles it) and "Back: menu" on the right. Returns what the template says changed.
// `canGoBack`: a view lies below this one (Session::canGoBack); the band says Back returns to it.
DirtyRects renderFrame(TemplateRenderer& renderer, Gfx& gfx, bool darkMode, bool canGoBack = false);

}  // namespace mirror
