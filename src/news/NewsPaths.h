#pragma once

#include <cstddef>
#include <cstdio>

// On-SD layout for the downloaded news. Everything under NEWS_DIR is disposable
// cache: a sync purges the directory before it writes, and a missing manifest
// simply means "not synced".
namespace NewsPaths {

inline constexpr const char* DIR = "/.crosspoint/news";
inline constexpr const char* TMP_FEED = "/.crosspoint/news/tmp.xml";
inline constexpr const char* MANIFEST = "/.crosspoint/news/manifest";

// feedN.idx holds fixed-size records, feedN.txt the concatenated article
// bodies. N is the feed's slot in the store's unified order (built-ins first,
// then user feeds), so removing a user feed invalidates the manifest.
inline void indexPath(const size_t slot, char* out, const size_t outLen) {
  snprintf(out, outLen, "%s/feed%u.idx", DIR, static_cast<unsigned>(slot));
}
inline void bodyPath(const size_t slot, char* out, const size_t outLen) {
  snprintf(out, outLen, "%s/feed%u.txt", DIR, static_cast<unsigned>(slot));
}
inline void indexTempPath(const size_t slot, char* out, const size_t outLen) {
  snprintf(out, outLen, "%s/feed%u.idx.new", DIR, static_cast<unsigned>(slot));
}
inline void bodyTempPath(const size_t slot, char* out, const size_t outLen) {
  snprintf(out, outLen, "%s/feed%u.txt.new", DIR, static_cast<unsigned>(slot));
}

}  // namespace NewsPaths
