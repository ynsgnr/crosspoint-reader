#include "ota/BootGuard.h"

#include <algorithm>
#include <cstring>

namespace mirror::ota {
namespace {
constexpr uint8_t kVersion = 1;
constexpr size_t kRevMax = 12;
constexpr uint8_t kPending = 1, kRolledBack = 2;

uint8_t checksum(const uint8_t* d, size_t n) {
  uint8_t sum = 0x5A;
  for (size_t i = 0; i < n; ++i) sum = static_cast<uint8_t>((sum << 1 | sum >> 7) ^ d[i]);
  return sum;
}

bool isRev(const std::string& s) {
  return s.size() >= 7 && s.size() <= kRevMax &&
         std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
}  // namespace

void encodeGuard(const BootGuard& g, uint8_t out[kGuardBlobSize]) {
  std::memset(out, 0, kGuardBlobSize);
  out[0] = kVersion;
  out[1] = static_cast<uint8_t>((g.pending ? kPending : 0) | (g.rolledBack ? kRolledBack : 0));
  out[2] = g.tries;
  out[3] = g.prevSlot;
  std::memcpy(out + 4, g.rev.data(), std::min(g.rev.size(), kRevMax));
  out[kGuardBlobSize - 1] = checksum(out, kGuardBlobSize - 1);
}

bool decodeGuard(const uint8_t* in, size_t len, BootGuard& out) {
  if (!in || len != kGuardBlobSize || in[0] != kVersion || in[kGuardBlobSize - 1] != checksum(in, kGuardBlobSize - 1) ||
      (in[1] & ~(kPending | kRolledBack)) != 0)
    return false;
  BootGuard g;
  g.pending = in[1] & kPending;
  g.rolledBack = in[1] & kRolledBack;
  g.tries = in[2];
  g.prevSlot = in[3];
  g.rev.assign(reinterpret_cast<const char*>(in + 4), strnlen(reinterpret_cast<const char*>(in + 4), kRevMax));
  if (g.pending && !isRev(g.rev)) return false;
  out = std::move(g);
  return true;
}

void armGuard(BootGuard& g, uint8_t prevSlot, const std::string& rev) {
  g.pending = true;
  g.rolledBack = false;
  g.tries = 0;
  g.prevSlot = prevSlot;
  g.rev = rev;
}

void disarmGuard(BootGuard& g) {
  g.pending = false;
  g.tries = 0;
}

BootAction onBootGuard(BootGuard& g, const std::string& runningRev) {
  if (!g.pending) return BootAction::Nothing;
  if (g.rev != runningRev) {  // not the build that was installed: a record must never act on another image
    disarmGuard(g);
    return BootAction::Stale;
  }
  if (g.tries >= kMaxBootTries) return BootAction::Rollback;
  ++g.tries;
  return BootAction::Proceed;
}

bool guardConfirmDue(const BootGuard& g, uint32_t uptimeMs) { return g.pending && uptimeMs >= kConfirmAfterMs; }

void confirmGuard(BootGuard& g) {
  g.pending = false;
  g.tries = 0;
}

void rolledBackGuard(BootGuard& g) {
  g.pending = false;
  g.tries = 0;
  g.rolledBack = true;
}

void giveUpGuard(BootGuard& g) {
  g.pending = false;
  g.tries = 0;
}

}  // namespace mirror::ota
