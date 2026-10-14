#include "news/NewsDiscovery.h"

#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <string>

#include "network/HttpDownloader.h"

namespace {

// Enough of a page to cover the <head> of the sites tested. Some CMS templates
// push the feed link past 8KB behind inlined preload tags, so this reaches
// further than the head alone usually needs. Freed before the feed is stored.
constexpr size_t HEAD_BUFFER_SIZE = 16384;
// A feed's own opening bytes are all we need to recognise one.
constexpr size_t PROBE_BUFFER_SIZE = 1024;

// Pulls at most `limit` bytes of a URL into buf. The callback returns false
// once it has enough, which aborts the transfer -- so a false return from
// fetchUrl is only a real failure when nothing arrived.
size_t fetchHead(const char* url, char* buf, const size_t limit) {
  size_t filled = 0;
  HttpDownloader::fetchUrl(std::string(url), [&](const uint8_t* data, const size_t len) {
    const size_t room = limit - filled;
    const size_t take = (len < room) ? len : room;
    memcpy(buf + filled, data, take);
    filled += take;
    return filled < limit;
  });
  return filled;
}

}  // namespace

namespace NewsDiscovery {

namespace {

// Tries the conventional paths under one origin. True when one served a feed.
bool probeOrigin(const char* origin, char* buffer, char* outUrl, const size_t outLen) {
  for (const char* path : PROBE_PATHS) {
    char probe[192];
    if (strlen(origin) + strlen(path) + 1 > sizeof(probe)) continue;
    snprintf(probe, sizeof(probe), "%s%s", origin, path);
    const size_t filled = fetchHead(probe, buffer, PROBE_BUFFER_SIZE);
    if (filled == 0) continue;
    if (!looksLikeFeedHead(buffer, filled)) continue;
    if (strlen(probe) + 1 > outLen) continue;
    strcpy(outUrl, probe);
    LOG_INF("NEWS", "Discovered feed by probe: %s", outUrl);
    return true;
  }
  return false;
}

}  // namespace

Result discoverFeedUrl(const char* input, char* outUrl, const size_t outLen) {
  char url[192];
  if (!normalizeInputUrl(input, url, sizeof(url))) return Result::NotFound;

  auto buffer = makeUniqueNoThrow<char[]>(HEAD_BUFFER_SIZE);
  if (!buffer) {
    LOG_ERR("NEWS", "OOM: %zu bytes for discovery buffer", HEAD_BUFFER_SIZE);
    return Result::FetchFailed;
  }

  size_t filled = fetchHead(url, buffer.get(), HEAD_BUFFER_SIZE);
  if (filled == 0) {
    // normalizeInputUrl assumes https for a bare host; small sites and a few
    // feed-only subdomains still answer on http alone.
    char plain[192];
    if (strncmp(url, "https://", 8) == 0 && strlen(url) - 1 < sizeof(plain)) {
      snprintf(plain, sizeof(plain), "http://%s", url + 8);
      filled = fetchHead(plain, buffer.get(), HEAD_BUFFER_SIZE);
      if (filled > 0) snprintf(url, sizeof(url), "%s", plain);
    }
  }
  if (filled == 0) {
    LOG_DBG("NEWS", "Discovery fetch failed: %s", url);
    return Result::FetchFailed;
  }

  // Already a feed.
  if (looksLikeFeedHead(buffer.get(), filled)) {
    if (strlen(url) + 1 > outLen) return Result::NotFound;
    strcpy(outUrl, url);
    return Result::Ok;
  }

  // A page that advertises its feed.
  if (findAlternateLink(buffer.get(), filled, url, outUrl, outLen)) {
    LOG_INF("NEWS", "Discovered advertised feed: %s", outUrl);
    return Result::Ok;
  }

  char origin[160];
  if (!urlOrigin(url, origin, sizeof(origin))) return Result::NotFound;
  if (probeOrigin(origin, buffer.get(), outUrl, outLen)) return Result::Ok;

  // Sites routinely serve the feed from only one of example.com /
  // www.example.com, and a redirect on the page does not move the probe path.
  char alternate[160];
  if (urlWithWwwToggled(origin, alternate, sizeof(alternate)) && probeOrigin(alternate, buffer.get(), outUrl, outLen)) {
    return Result::Ok;
  }

  LOG_DBG("NEWS", "No feed found for %s", url);
  return Result::NotFound;
}

}  // namespace NewsDiscovery
