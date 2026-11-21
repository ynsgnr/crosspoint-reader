#pragma once
// How long a BLE link may stay unsecured before the device drops it. One stranger that connects and says nothing must
// not hold the only slot (the real phone is refused while a link is up).
#include <cstdint>

namespace mirror {

struct LinkGuard {
  static constexpr uint32_t kSecureWithinMs = 5000;    // bonded phone: encryption starts within moments of connecting
  static constexpr uint32_t kPairingWithinMs = 60000;  // pairing: the user types the passkey on the phone

  // `pairing`: the window is open, or a passkey was issued. Wrap-safe for millis().
  static bool expired(uint32_t nowMs, uint32_t connectedAtMs, bool secure, bool pairing) {
    if (secure) return false;
    return static_cast<uint32_t>(nowMs - connectedAtMs) > (pairing ? kPairingWithinMs : kSecureWithinMs);
  }
};

}  // namespace mirror
