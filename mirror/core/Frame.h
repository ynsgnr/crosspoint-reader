#pragma once
// One panel frame: a status band on top, the template below it. Shared by the device screen and the simulator.
#include <optional>
#include <string>

#include "core/Gfx.h"
#include "core/TemplateRenderer.h"
#include "protocol_gen.h"

namespace mirror {

int statusBandHeight(const Gfx& gfx);

// Dark mode state and the button hints (tap the band: dark mode; Back: the menu with the launcher and unlock).
std::string statusBandText(const std::optional<gen::PhoneStatus>& phone);

// Draws the template under the band, then the band text. Returns what the template says changed.
DirtyRects renderFrame(TemplateRenderer& renderer, Gfx& gfx, const std::string& bandText);

}  // namespace mirror
