#pragma once

#include <cstddef>
#include <cstdint>

// FNV-1a, 32 bit. Feed a file through it in chunks, passing the previous result as `hash`,
// to tell whether a downloaded image changed without keeping the old copy.
constexpr uint32_t FNV1A32_SEED = 2166136261u;

inline uint32_t fnv1a32(const uint8_t* data, const size_t length, uint32_t hash = FNV1A32_SEED) {
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}
