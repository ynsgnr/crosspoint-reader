#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mirror::sim {
// Writes an 8-bit RGB PNG using stored (uncompressed) deflate blocks: no zlib dependency.
bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb);
}  // namespace mirror::sim
