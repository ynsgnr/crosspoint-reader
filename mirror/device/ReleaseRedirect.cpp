#if defined(CROSSPOINT_MIRROR)
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's ip4_addr.h unless seen first (see
// MirrorUpdateActivity.cpp).
#include "network/HttpDownloader.h"
// clang-format off
#include "device/ReleaseRedirect.h"
// clang-format on
#include <Logging.h>
#include <SecureClient.h>

#include "core/ReleaseRedirect.h"
#include "network/WifiPowerSaveGuard.h"
#include "ota/OtaConfig.h"

namespace mirror::ota {
namespace {
constexpr size_t kMaxHead = 12288;     // GitHub's answer is about 6 KB
constexpr uint32_t kAnswerMs = 20000;  // from the request to the end of the headers
}  // namespace

Resolve resolveReleaseUrl(const std::string& url, std::string& resolved) {
  resolved = url;
  if (url.rfind(kReleaseBase, 0) != 0) return Resolve::Ok;  // not a GitHub release address
  std::string host, path;
  if (!splitHttpsUrl(url, host, path)) return Resolve::Failed;

  WifiPowerSaveGuard psGuard;
  std::string head;
  head.reserve(kMaxHead);  // once, up front: a failed allocation while it grows would abort the device
  bool complete = false;
  {
    freeink::SecureClient client;
    client.setInsecure();  // as the rest of the firmware's HTTP client; the image is checked against the signed list
                           // instead
    if (client.connect(host.c_str(), 443)) {
      const std::string request = releaseRequest(host, path);
      client.write(reinterpret_cast<const uint8_t*>(request.data()), request.size());
      uint8_t buf[256];
      const uint32_t until = millis() + kAnswerMs;
      while (!complete && head.size() < kMaxHead && static_cast<int32_t>(until - millis()) > 0) {
        const int n = client.available() > 0 ? client.read(buf, sizeof(buf)) : 0;
        if (n > 0) {
          head.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
          complete = headComplete(head);
        } else if (!client.connected()) {
          break;
        } else {
          delay(10);
        }
      }
    }
    client.stop();
  }
  if (!complete) {
    LOG_ERR("OTA", "release redirect: no complete answer from %s (%u bytes)", host.c_str(),
            static_cast<unsigned>(head.size()));
    return Resolve::Failed;
  }
  const RedirectHead answer = parseRedirectHead(head);
  if (answer.status == 404) return Resolve::NotFound;
  if (answer.status == 200) return Resolve::Ok;  // served directly: the normal download takes it from here
  const bool redirect = answer.status == 301 || answer.status == 302 || answer.status == 303 || answer.status == 307 ||
                        answer.status == 308;
  if (!redirect || !isGithubAssetUrl(answer.location)) {
    LOG_ERR("OTA", "release redirect: status %d, target %s", answer.status,
            redirect ? "not a GitHub asset host" : "none");
    return Resolve::Failed;
  }
  resolved = answer.location;
  return Resolve::Ok;
}

}  // namespace mirror::ota
#endif
