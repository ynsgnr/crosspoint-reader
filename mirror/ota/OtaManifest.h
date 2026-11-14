#pragma once
// The list of installable builds ("channels") that CI publishes, one text file per board (channels-<board>.txt), signed
// with ECDSA P-256 over the exact bytes. The device checks the signature before it looks at a single character.
//
// One line per channel:   <id> <rev> <built> <size> <sha256> <title>
//   id      "develop" (the develop-retink branch) or "pr-<number>"
//   rev     the first 8 hex digits of the commit the image was built from (MIRROR_GIT_REV of that image)
//   built   unix seconds
//   size    bytes of the image
//   sha256  64 lower-case hex digits of the image file
//   title   the rest of the line (a pull request title)
// The image is <release>/<id>-<board>.bin; the device builds that name itself, the file never names a URL.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mirror::ota {

struct Channel {
  std::string id;
  std::string rev;
  std::string sha256;
  std::string title;
  uint32_t built = 0;
  uint32_t size = 0;
};

constexpr size_t kMaxChannels = 8;
constexpr size_t kMaxManifestBytes = 2048;

// Checks a signature over `msg`. The device uses ECDSA P-256 / SHA-256 (device/OtaVerify.cpp); tests use a fake.
class SignatureVerifier {
 public:
  virtual ~SignatureVerifier() = default;
  virtual bool verify(const uint8_t* msg, size_t msgLen, const uint8_t* sig, size_t sigLen) const = 0;
};

enum class Trust { Ok, BadSignature, BadFormat };

// Verifies the signature FIRST and only then reads the text. Any line that is not exactly the format above makes the
// whole file BadFormat (a file this firmware did not expect is not trusted half way).
Trust openManifest(const std::string& text, const uint8_t* sig, size_t sigLen, const SignatureVerifier& verifier,
                   std::vector<Channel>& out);

// "develop" or "pr-<1..6 digits>".
bool isChannelId(const std::string& id);

// The channel is a different build from the running one. `runningRev` is MIRROR_GIT_REV: 8 hex digits, "+" appended
// when the tree had uncommitted changes (such a build is never "the same").
bool differsFromRunning(const Channel& c, const std::string& runningRev);

}  // namespace mirror::ota
