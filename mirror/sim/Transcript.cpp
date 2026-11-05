#include "sim/Transcript.h"

#include <deque>
#include <map>
#include <sstream>

namespace mirror::sim {

std::vector<uint8_t> fromHex(const std::string& h) {
  std::vector<uint8_t> b;
  for (size_t i = 0; i + 1 < h.size(); i += 2)
    b.push_back(static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return b;
}

std::string toHex(const std::vector<uint8_t>& b) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (uint8_t c : b) {
    o += d[c >> 4];
    o += d[c & 15];
  }
  return o;
}

bool replay(const std::string& transcript, const TemplateRegistry& reg, const ReplayHooks& hooks) {
  std::deque<std::string> out;  // frames the device emitted, waiting to be matched by `rx` lines
  Session session(reg, [&out](const std::vector<uint8_t>& f) { out.push_back(toHex(f)); });
  bool ok = true;
  auto fail = [&](const std::string& m) {
    ok = false;
    if (hooks.fail) hooks.fail(m);
  };
  std::istringstream in(transcript);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    ++n;
    if (line.empty() || line[0] == '#') continue;
    const size_t sp = line.find(' ');
    const std::string cmd = line.substr(0, sp), arg = sp == std::string::npos ? "" : line.substr(sp + 1);
    if (cmd == "tx" || cmd == "corrupt") {
      const auto bytes = fromHex(arg);
      session.feed(bytes.data(), bytes.size());
    } else if (cmd == "drop") {
      // lost in transit: the device never sees it
    } else if (cmd == "rx") {
      if (out.empty())
        fail("line " + std::to_string(n) + ": expected device frame " + arg + " but none was emitted");
      else {
        if (out.front() != arg)
          fail("line " + std::to_string(n) + ": device frame mismatch\n  want " + arg + "\n  got  " + out.front());
        out.pop_front();
      }
    } else if (cmd == "btn") {
      static const std::map<std::string, Button> kButtons = {
          {"up", Button::Up},           {"down", Button::Down}, {"left", Button::Left},    {"right", Button::Right},
          {"confirm", Button::Confirm}, {"top", Button::Top},   {"bottom", Button::Bottom}};
      const auto b = kButtons.find(arg);
      if (b == kButtons.end())
        fail("line " + std::to_string(n) + ": unknown button " + arg);
      else
        session.onButton(b->second);
    } else if (cmd == "cmd") {
      session.sendCommand(static_cast<uint8_t>(std::stoi(arg)));
    } else if (cmd == "phone") {
      if (session.phoneDump() != arg)
        fail("line " + std::to_string(n) + ": phone status mismatch\n  want " + arg + "\n  got  " +
             session.phoneDump());
    } else if (cmd == "state") {
      const std::string got = session.active() ? session.active()->dump() : "-";
      if (!out.empty()) fail("line " + std::to_string(n) + ": unexpected device frame " + out.front()), out.clear();
      if (got != arg) fail("line " + std::to_string(n) + ": state mismatch\n  want " + arg + "\n  got  " + got);
    }
    if (hooks.afterLine) hooks.afterLine(line, session);
  }
  if (!out.empty()) fail("unmatched trailing device frames");
  return ok;
}

}  // namespace mirror::sim
