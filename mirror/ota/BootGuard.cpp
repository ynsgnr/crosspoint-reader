#include "ota/BootGuard.h"

#include <algorithm>
#include <cstring>

namespace mirror::ota {
namespace {
constexpr size_t kRevMax = 12;

bool isRev(const std::string& s) {
  return s.size() >= 7 && s.size() <= kRevMax &&
         std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
}  // namespace

// Layout: [0] pending, [1] tries, [2] prevSlot, [3..15] revision text, zero padded.
void encodeGuard(const BootGuard& g, uint8_t out[kGuardBlobSize]) {
  std::memset(out, 0, kGuardBlobSize);
  out[0] = g.pending ? 1 : 0;
  out[1] = g.tries;
  out[2] = g.prevSlot;
  std::memcpy(out + 3, g.rev.data(), std::min(g.rev.size(), kRevMax));
}

bool decodeGuard(const uint8_t* in, size_t len, BootGuard& out) {
  if (!in || len != kGuardBlobSize || in[0] > 1) return false;
  BootGuard g;
  g.pending = in[0] == 1;
  g.tries = in[1];
  g.prevSlot = in[2];
  g.rev.assign(reinterpret_cast<const char*>(in + 3), strnlen(reinterpret_cast<const char*>(in + 3), kRevMax));
  if (g.pending && !isRev(g.rev)) return false;
  out = std::move(g);
  return true;
}

void armGuard(BootGuard& g, uint8_t prevSlot, const std::string& rev) {
  g.pending = true;
  g.tries = 0;
  g.prevSlot = prevSlot;
  g.rev = rev;
}

void endTrial(BootGuard& g) {
  g.pending = false;
  g.tries = 0;
}

BootAction onBootGuard(BootGuard& g, const std::string& runningRev) {
  if (!g.pending) return BootAction::Nothing;
  if (g.rev != runningRev) {  // not the build that was installed: a record must never act on another image
    endTrial(g);
    return BootAction::Stale;
  }
  if (g.tries >= kMaxBootTries) return BootAction::Rollback;
  ++g.tries;
  return BootAction::Proceed;
}

bool guardConfirmDue(const BootGuard& g, uint32_t uptimeMs) { return g.pending && uptimeMs >= kConfirmAfterMs; }

bool rollbackFailedGuard(BootGuard& g) {
  ++g.tries;
  if (g.tries < kMaxBootTries + kMaxRollbackAttempts) return false;
  endTrial(g);
  return true;
}

}  // namespace mirror::ota
