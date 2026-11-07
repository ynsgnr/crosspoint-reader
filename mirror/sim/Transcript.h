#pragma once
// Replays golden transcripts (golden/chat/*.txt from the Android repo) through the real Session.
#include <functional>
#include <string>
#include <vector>

#include "core/Session.h"

namespace mirror::sim {

std::vector<uint8_t> fromHex(const std::string& hex);
std::string toHex(const std::vector<uint8_t>& b);

struct ReplayHooks {
  // Called after each line that changed or may have changed what the device shows.
  std::function<void(const std::string& line, Session&)> afterLine;
  // Called when a transcript expectation fails.
  std::function<void(const std::string& message)> fail;
};

// True when every expectation (rx frames, state dumps) held. Works for any registered template.
bool replay(const std::string& transcript, const TemplateRegistry& reg, const ReplayHooks& hooks);

}  // namespace mirror::sim
