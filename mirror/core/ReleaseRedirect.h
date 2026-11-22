#pragma once
// GitHub answers a release download with a 302 whose Content-Security-Policy header alone is over 4 KB, and the FreeInk
// SDK's HTTP client refuses header lines over 4096 bytes (SecureHttpClient::MAX_LINE): the request then fails with
// status -1 and the updater says it cannot reach the update server. The updater therefore reads that one redirect
// itself (device/ReleaseRedirect.cpp) and hands the address it names to the normal downloader. The parsing and the
// checks are here, so they are host-tested.
#include <string>

namespace mirror {

// "https://host/path" with a plain host name (letters, digits, dots, hyphens; no port, no user info). False for
// anything else.
bool splitHttpsUrl(const std::string& url, std::string& host, std::string& path);

// The request line and headers; the connection is closed after the answer.
std::string releaseRequest(const std::string& host, const std::string& path);

// True once the blank line that ends the response head has arrived.
bool headComplete(const std::string& head);

struct RedirectHead {
  int status = -1;  // -1: not an HTTP response
  std::string location;
};
// The status and the Location of a response head. Header lines of any length are fine.
RedirectHead parseRedirectHead(const std::string& head);

// An address the updater may follow from a release download: https, a host under githubusercontent.com, a bounded
// length.
bool isGithubAssetUrl(const std::string& url);

}  // namespace mirror
