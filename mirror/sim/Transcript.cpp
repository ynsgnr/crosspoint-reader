#include "sim/Transcript.h"

#include <deque>
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

bool replay(const std::string& transcript, const TemplateRegistry& reg, const ReplayHooks& hooks,
            const std::function<std::string(Session&)>& dump) {
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
      session.onButton(arg == "up" ? Button::Up : Button::Down);
    } else if (cmd == "state") {
      const std::string got = session.active() ? dump(session) : "";
      if (!out.empty()) fail("line " + std::to_string(n) + ": unexpected device frame " + out.front()), out.clear();
      if (got != arg && !(arg.rfind("T: U:0 S:0 D: M:", 0) == 0 && !session.active()))
        fail("line " + std::to_string(n) + ": state mismatch\n  want " + arg + "\n  got  " + got);
    }
    if (hooks.afterLine) hooks.afterLine(line, session);
  }
  if (!out.empty()) fail("unmatched trailing device frames");
  return ok;
}

}  // namespace mirror::sim
