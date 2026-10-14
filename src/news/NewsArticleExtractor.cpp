#include "news/NewsArticleExtractor.h"

#include <Logging.h>

#include <string>

#include "network/HttpDownloader.h"

namespace NewsArticleFetch {

Result fetch(const char* url, char* out, const size_t capacity, size_t& outLen) {
  outLen = 0;
  if (!url || url[0] == '\0' || capacity == 0) return Result::Unreachable;

  NewsArticleExtractor extractor(out, capacity);
  size_t read = 0;
  HttpDownloader::fetchUrl(std::string(url), [&](const uint8_t* data, const size_t len) {
    read += len;
    extractor.addChunk(reinterpret_cast<const char*>(data), len);
    // Aborting once the story's </article> has been seen saves the page's
    // footer and its related-links block, which is most of what is left.
    return !extractor.done() && read < MAX_PAGE_BYTES;
  });

  // A false return from the callback aborts the transfer, and the downloader
  // reports that the same way it reports a failure -- so the bytes that arrived
  // are the success signal, exactly as NewsDiscovery reads it.
  if (read == 0) {
    LOG_ERR("NEWS", "full text: %s unreachable", url);
    return Result::Unreachable;
  }

  outLen = extractor.finish();
  LOG_INF("NEWS", "full text: %u page bytes, %u text%s", static_cast<unsigned>(read), static_cast<unsigned>(outLen),
          extractor.done() ? " (article)" : "");
  if (outLen < NewsArticleExtractor::MIN_ARTICLE_BYTES) {
    // A consent wall, a bot check, or a page that builds its body in the
    // browser. Nothing here is worth showing instead of the feed's summary.
    outLen = 0;
    return Result::NoText;
  }
  return Result::Ok;
}

}  // namespace NewsArticleFetch
