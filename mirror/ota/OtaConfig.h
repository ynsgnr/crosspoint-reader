#pragma once
// Where the manifest and the images come from: the release tagged "ota" of the fork's repository, which CI keeps up to
// date (mirror/scripts/ota/publish.sh uses the same file names). One manifest per board.
#include <cstdint>
#include <string>

namespace mirror::ota {

inline constexpr char kReleaseBase[] = "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/";
inline constexpr uint16_t kMaxSignatureBytes = 128;  // a DER ECDSA P-256 signature is 70-72 bytes
inline constexpr unsigned kMinBatteryPercent = 25;   // power loss in the middle of the flash
inline constexpr char kStagePath[] = "/.mirror-ota.bin";

inline std::string manifestUrl(const std::string& board) {
  return kReleaseBase + std::string("channels-") + board + ".txt";
}
inline std::string signatureUrl(const std::string& board) { return manifestUrl(board) + ".sig"; }
inline std::string imageUrl(const std::string& channelId, const std::string& board) {
  return kReleaseBase + channelId + "-" + board + ".bin";
}

}  // namespace mirror::ota
