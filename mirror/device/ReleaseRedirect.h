#pragma once
#if defined(CROSSPOINT_MIRROR)
// Resolves a GitHub release address to the address GitHub redirects it to (see core/ReleaseRedirect.h for why the stock
// HTTP client cannot do this one hop). Other addresses are returned unchanged.
#include <string>

namespace mirror::ota {

enum class Resolve { Ok, NotFound, Failed };

// `resolved` is the address to download from. It is only valid for a few minutes (GitHub signs it), so resolve right
// before downloading. NotFound: GitHub says there is no such release asset.
Resolve resolveReleaseUrl(const std::string& url, std::string& resolved);

}  // namespace mirror::ota
#endif
