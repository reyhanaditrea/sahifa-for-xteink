#pragma once

#include <cstddef>

// Turns whatever the user typed into a feed URL.
//
// People know their news site, not its feed path, so "bbc.com" and
// "news.detik.com" have to work as well as a full .xml URL. The device fetches
// the page head, looks for the <link rel="alternate"> the site advertises, and
// falls back to the handful of conventional paths.
namespace NewsDiscovery {

enum class Result { Ok, FetchFailed, NotFound };

// Fetches over the network (needs Wi-Fi up). outUrl receives the resolved feed
// URL; input may be bare ("news.detik.com"), in which case https:// is assumed.
Result discoverFeedUrl(const char* input, char* outUrl, size_t outLen);

// --- pure helpers, unit-tested on the host --------------------------------

// True when the first bytes of a response look like an RSS/Atom/RDF document
// rather than a web page. Skips a UTF-8 BOM and leading whitespace.
bool looksLikeFeedHead(const char* buf, size_t len);

// Finds <link rel="alternate" type="application/rss+xml" href="..."> in a page
// head. Handles either attribute order, single or double quotes, and any case.
// A relative href is resolved against baseUrl. Returns false when absent.
bool findAlternateLink(const char* html, size_t len, const char* baseUrl, char* out, size_t outLen);

// Writes scheme://host[:port] of url into out. False when url has no host.
bool urlOrigin(const char* url, char* out, size_t outLen);

// Copies input into out, prefixing https:// when no scheme is present and
// stripping a trailing slash-free fragment. False when the result would not fit.
bool normalizeInputUrl(const char* input, char* out, size_t outLen);

// Host part of a URL ("news.detik.com"), used as the feed's initial display
// name until the first sync reads the real <title>. A leading "www." is
// dropped. False when url has no host.
bool urlHost(const char* url, char* out, size_t outLen);

// Conventional feed paths, probed in order when a page advertises nothing.
// "/feed/" and "?feed=rss2" cover WordPress, which is most small sites.
inline constexpr const char* PROBE_PATHS[] = {"/feed",      "/feed/",    "/rss",       "/rss.xml",
                                              "/feed.xml",  "/atom.xml", "/index.xml", "/feeds/posts/default",
                                              "/?feed=rss2"};

// Host with "www." added, or removed when it was already there. Sites commonly
// serve the feed from only one of the two. False when url has no host.
bool urlWithWwwToggled(const char* url, char* out, size_t outLen);

}  // namespace NewsDiscovery
