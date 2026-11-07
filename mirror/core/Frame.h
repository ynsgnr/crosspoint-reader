#pragma once
// One panel frame: a status band on top, the template below it. Shared by the device screen and the simulator.
#include <optional>
#include <string>

#include "core/Gfx.h"
#include "core/TemplateRenderer.h"
#include "protocol_gen.h"

namespace mirror {

int statusBandHeight(const Gfx& gfx);

// The band: the phone's dark mode as a dot on the left (tapping the band toggles it) and a hint on the right.
bool bandDarkMode(const std::optional<gen::PhoneStatus>& phone);
std::string statusBandText(const std::optional<gen::PhoneStatus>& phone);  // the hint: "Back: menu"

// Draws the template under the band, then the band: a dot on the left (filled = dark mode on, hollow = off) and
// `bandText` on the right. Returns what the template says changed.
DirtyRects renderFrame(TemplateRenderer& renderer, Gfx& gfx, const std::string& bandText, bool darkMode = false);

}  // namespace mirror
