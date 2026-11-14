#pragma once
// The OTA channel manifest: which firmware builds the device may offer. CI publishes it next to the images and signs it
// (ECDSA P-256 over the exact bytes). The device checks the signature before it reads a single field, then applies its
// own policy on top, so even a correctly signed manifest cannot point it at another host, board or a huge image.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mirror::ota {

// One installable build: the develop branch ("develop") or an open pull request ("pr-<number>").
struct Channel {
  std::string id;
  std::string title;   // what the build is, for the menu (a PR title)
  std::string rev;     // git commit the image was built from (8 hex digits), shown on the About page as MIRROR_GIT_REV
  std::string board;   // board tag the image is for (board_tag::boardName(), "x4pro")
  std::string url;     // https, under Policy::urlPrefix
  std::string sha256;  // 64 lowercase hex digits of the whole image file
  uint32_t size = 0;   // bytes
  uint32_t built = 0;  // unix seconds
};

struct Manifest {
  uint32_t issued = 0;  // unix seconds; never goes back (replay guard)
  std::vector<Channel> channels;
};

// Checks a signature over `msg`. The device uses ECDSA P-256 / SHA-256 (device/OtaVerify.cpp); tests use a fake.
class SignatureVerifier {
 public:
  virtual ~SignatureVerifier() = default;
  virtual bool verify(const uint8_t* msg, size_t msgLen, const uint8_t* sig, size_t sigLen) const = 0;
};

// What this device accepts, independent of what the manifest says.
struct Policy {
  std::string board;      // only images for this board
  std::string urlPrefix;  // image URLs must start with this (https://github.com/<fork>/releases/download/ota/)
  uint32_t maxImageBytes = 0;
  uint32_t minImageBytes = 512 * 1024;
  uint32_t lastIssued = 0;  // the newest manifest accepted so far: an older one is a replay
};

enum class Trust {
  Ok,
  BadSignature,  // the bytes are not signed by our key (or no key is configured)
  BadDocument,   // signed, but not a manifest this firmware understands
  Stale,         // signed, but older than one already accepted
};

// Verifies the signature FIRST and only then parses. Channels the policy rejects are dropped (a build for another
// board is not an error, just not offered); at most kMaxChannels are kept, develop first, then pull requests newest
// first.
Trust openManifest(const std::string& json, const uint8_t* sig, size_t sigLen, const SignatureVerifier& verifier,
                   const Policy& policy, Manifest& out);

constexpr size_t kMaxChannels = 8;

// "develop" or "pr-<1..6 digits>".
bool isChannelId(const std::string& id);
// `policy` accepts this channel (every field checked; see the .cpp).
bool channelAllowed(const Channel& c, const Policy& policy);

// The channel is a different build from the running one. `runningRev` is MIRROR_GIT_REV: 8 hex digits, "+" appended
// when the tree had uncommitted changes (such a build is never "the same").
bool differsFromRunning(const Channel& c, const std::string& runningRev);

const char* trustName(Trust t);

}  // namespace mirror::ota
