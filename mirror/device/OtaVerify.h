#pragma once
#if defined(CROSSPOINT_MIRROR)
// Device side of the OTA checks: the ECDSA signature of the manifest and the SHA-256 of the downloaded image.
#include <cstddef>
#include <string>

#include "ota/OtaManifest.h"

namespace mirror::ota {

// ECDSA P-256 / SHA-256 against kOtaPublicKeyDer (ota/OtaKey.h). The signature is the DER form `openssl dgst -sign`
// writes. Anything that is not a valid signature by that key, including a key that does not parse, is a refusal.
class EcdsaVerifier final : public SignatureVerifier {
 public:
  bool verify(const uint8_t* msg, size_t msgLen, const uint8_t* sig, size_t sigLen) const override;
};

// SHA-256 of the whole file as 64 lowercase hex digits, and its size. False when the file cannot be read to its end.
bool sha256OfFile(const char* sdPath, std::string& hexOut, size_t& sizeOut);

}  // namespace mirror::ota
#endif
