#if defined(CROSSPOINT_MIRROR)
#include "device/OtaVerify.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include <algorithm>

#include "ota/OtaKey.h"

namespace mirror::ota {
namespace {

std::string toHex(const uint8_t* d, size_t n) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    out += digits[d[i] >> 4];
    out += digits[d[i] & 15];
  }
  return out;
}

}  // namespace

bool EcdsaVerifier::verify(const uint8_t* msg, size_t msgLen, const uint8_t* sig, size_t sigLen) const {
  uint8_t hash[32];
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  const bool hashed = mbedtls_sha256_starts(&sha, /*is224=*/0) == 0 && mbedtls_sha256_update(&sha, msg, msgLen) == 0 &&
                      mbedtls_sha256_finish(&sha, hash) == 0;
  mbedtls_sha256_free(&sha);
  if (!hashed) return false;

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, kOtaPublicKeyDer, kOtaPublicKeyDerLen);
  if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sigLen);
  mbedtls_pk_free(&pk);
  if (rc != 0) LOG_ERR("OTA", "signature check failed: -0x%04x", static_cast<unsigned>(-rc));
  return rc == 0;
}

bool sha256OfFile(const char* sdPath, std::string& hexOut, size_t& sizeOut) {
  constexpr size_t kChunk = 4096;
  auto buf = makeUniqueNoThrow<uint8_t[]>(kChunk);
  if (!buf) return false;
  HalFile file;
  if (!Storage.openFileForRead("OTA", sdPath, file) || !file) return false;
  const size_t total = file.fileSize();

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts(&sha, /*is224=*/0) == 0;
  size_t remaining = total;
  while (ok && remaining > 0) {
    const size_t want = std::min(kChunk, remaining);
    const int got = file.read(buf.get(), want);
    ok = got > 0 && static_cast<size_t>(got) == want && mbedtls_sha256_update(&sha, buf.get(), want) == 0;
    remaining -= ok ? want : 0;
  }
  uint8_t digest[32];
  ok = ok && mbedtls_sha256_finish(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  file.close();
  if (!ok) return false;
  hexOut = toHex(digest, sizeof(digest));
  sizeOut = total;
  return true;
}

}  // namespace mirror::ota
#endif
