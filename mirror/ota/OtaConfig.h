#pragma once
// Where the OTA manifest and the images come from: the release tagged "ota" of the fork's repository. CI
// (.github/workflows/ota-publish.yml) keeps channels.json, its signature and the images there.
#include <cstdint>

namespace mirror::ota {

inline constexpr char kReleaseBase[] = "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/";
inline constexpr char kManifestUrl[] =
    "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/channels.json";
inline constexpr char kSignatureUrl[] =
    "https://github.com/ynsgnr/crosspoint-reader/releases/download/ota/channels.json.sig";
inline constexpr uint16_t kMaxSignatureBytes = 128;  // a DER ECDSA P-256 signature is 70-72 bytes
inline constexpr unsigned kMinBatteryPercent = 25;   // an update that runs out of power half way is the one real risk
inline constexpr char kStagePath[] = "/.mirror-ota.bin";

}  // namespace mirror::ota
