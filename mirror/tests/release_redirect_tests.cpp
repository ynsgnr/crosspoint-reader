// Host tests for reading GitHub's release redirect, including the head that the SDK's HTTP client cannot read.
#include <cstdio>
#include <string>

#include "core/ReleaseRedirect.h"

using namespace mirror;

#define RCHECK(c)                                              \
  do {                                                         \
    ++checks;                                                  \
    if (!(c)) {                                                \
      ++failed;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                          \
  } while (0)

namespace {
const char* const kCdn =
    "https://release-assets.githubusercontent.com/github-production-release-asset/1415283614/"
    "61aeecc6?sp=r&sv=2018-11-09&sr=b&se=2026-11-22T19%3A00%3A00Z";

// What github.com sent for a release download on 2026-11-22: 15 headers, 5.7 KB, one of them 4,203 bytes.
std::string githubHead() {
  return "HTTP/1.1 302 Found\r\nContent-Type: text/html; charset=utf-8\r\nVary: X-PJAX, X-PJAX-Container\r\n"
         "Content-Security-Policy: default-src 'none'; " +
         std::string(4150, 'x') + "\r\nLocation: " + kCdn + "\r\nStrict-Transport-Security: max-age=31536000\r\n\r\n";
}
}  // namespace

int runReleaseRedirectTests(int& checks) {
  int failed = 0;
  {  // the head that broke the updater
    const std::string head = githubHead();
    RCHECK(head.find("Content-Security-Policy") != std::string::npos && head.size() > 4200);
    RCHECK(headComplete(head) && !headComplete(head.substr(0, head.size() - 2)));
    const RedirectHead r = parseRedirectHead(head);
    RCHECK(r.status == 302 && r.location == kCdn);
    RCHECK(isGithubAssetUrl(r.location));
  }
  {  // parsing details
    RCHECK(parseRedirectHead("HTTP/1.1 200 OK\r\nContent-Length: 72\r\n\r\n").status == 200);
    RCHECK(parseRedirectHead("HTTP/1.1 200 OK\r\nContent-Length: 72\r\n\r\n").location.empty());
    RCHECK(parseRedirectHead("HTTP/1.1 404 Not Found\r\n\r\n").status == 404);
    RCHECK(parseRedirectHead("HTTP/1.1 302 Found\r\nlocation:   https://a.githubusercontent.com/x\r\n\r\n").location ==
           "https://a.githubusercontent.com/x");
    RCHECK(parseRedirectHead("HTTP/1.1 302 Found\r\nX-Location: https://evil.example/\r\n\r\n").location.empty());
    RCHECK(parseRedirectHead("SSH-2.0-OpenSSH\r\n\r\n").status == -1);
    RCHECK(parseRedirectHead("").status == -1);
    RCHECK(parseRedirectHead("HTTP/1.1 302 Found\r\nLocation: https://a.githubusercontent.com/x").location ==
           "https://a.githubusercontent.com/x");
  }
  {  // only GitHub's asset hosts, only https
    RCHECK(isGithubAssetUrl("https://objects.githubusercontent.com/github-production-release-asset-2e65be/1/2?x=1"));
    RCHECK(isGithubAssetUrl("https://Release-Assets.GitHubUserContent.com/a"));
    RCHECK(!isGithubAssetUrl("http://release-assets.githubusercontent.com/a"));
    RCHECK(!isGithubAssetUrl("https://githubusercontent.com/a"));  // no sub-domain: not one of the asset hosts
    RCHECK(!isGithubAssetUrl("https://evil.example/githubusercontent.com/"));
    RCHECK(!isGithubAssetUrl("https://release-assets.githubusercontent.com.evil.example/a"));
    RCHECK(!isGithubAssetUrl("https://evilgithubusercontent.com/a"));
    RCHECK(!isGithubAssetUrl("https://user@release-assets.githubusercontent.com/a"));
    RCHECK(!isGithubAssetUrl("https://release-assets.githubusercontent.com:8443/a"));
    RCHECK(!isGithubAssetUrl("https://a.githubusercontent.com/a b"));
    RCHECK(!isGithubAssetUrl("https://a.githubusercontent.com"));  // no path
    RCHECK(!isGithubAssetUrl("https://a.githubusercontent.com/" + std::string(2100, 'a')));
    RCHECK(!isGithubAssetUrl(""));
  }
  {  // the request
    std::string host, path;
    RCHECK(splitHttpsUrl("https://github.com/o/r/releases/download/ota/channels-x4pro.txt", host, path));
    RCHECK(host == "github.com" && path == "/o/r/releases/download/ota/channels-x4pro.txt");
    RCHECK(releaseRequest(host, path) ==
           "GET /o/r/releases/download/ota/channels-x4pro.txt HTTP/1.1\r\nHost: github.com\r\nUser-Agent: "
           "CrossPoint-ESP32-ota\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    RCHECK(!splitHttpsUrl("http://github.com/a", host, path) && !splitHttpsUrl("https://github.com:443/a", host, path));
  }
  return failed;
}
