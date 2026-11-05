#pragma once
#include <cstdint>
#include <string>

namespace mirror::utf8 {

// Length in bytes of the UTF-8 sequence starting with lead byte b (1 for invalid bytes).
inline size_t seqLen(uint8_t b) {
  if (b < 0x80) return 1;
  if ((b >> 5) == 0x6) return 2;
  if ((b >> 4) == 0xE) return 3;
  if ((b >> 3) == 0x1E) return 4;
  return 1;
}

inline bool isBoundary(const std::string& s, size_t i) {
  return i >= s.size() || (static_cast<uint8_t>(s[i]) & 0xC0) != 0x80;
}

}  // namespace mirror::utf8
