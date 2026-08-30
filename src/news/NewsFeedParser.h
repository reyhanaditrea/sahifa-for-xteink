#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <cstdint>
#include <memory>

#include "news/NewsIndex.h"
#include "news/NewsTextSink.h"

// Reads a downloaded feed file off SD and writes the app's index + body files.
//
// The file is parsed from SD rather than straight off the socket: a feed can
// stall mid-transfer, and a half-parsed index is worse than a failed download.
// Parsing is stepped so the caller can keep the UI alive between chunks.
//
// Three dialects share one parser because they differ only in element names:
//   RSS 2.0   channel > item   (pubDate, description, content:encoded)
//   RSS 1.0   rdf:RDF > item   (dc:date) -- DW ships this, note items are NOT
//                               nested in channel, hence "item at any depth"
//   Atom      feed > entry     (updated/published, summary/content, link@href)
class NewsFeedParser {
 public:
  struct Result {
    uint16_t itemsKept = 0;
    uint16_t itemsSeen = 0;
  };

  NewsFeedParser();
  ~NewsFeedParser();
  NewsFeedParser(const NewsFeedParser&) = delete;
  NewsFeedParser& operator=(const NewsFeedParser&) = delete;

  // keepOrdinals lists the local day ordinals to keep (today, yesterday, the
  // day before). An item whose date is unknown is always kept -- a missing date
  // is far more common than a wrong one, and dropping it would empty some feeds
  // entirely. Pass a zeroed array to keep everything (clock never synced).
  bool begin(const char* feedPath, const char* indexPath, const char* bodyPath,
             const uint32_t keepOrdinals[NewsIndex::DAY_COUNT], uint32_t syncOrdinal, int32_t localOffsetSec);

  enum class Step : uint8_t { More, Done, Error };
  Step step();

  // Writes the header with the final count and closes the files.
  bool finish(Result& result);
  // Tears down without completing; the caller removes the partial files.
  void abort();

  // The feed's own <title>, available once parsing has passed the channel head.
  const char* feedTitle() const { return feedTitle_; }

 private:
  enum class Field : uint8_t { None, Title, Link, Date, Body, FeedTitle };

  static void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL onEnd(void* userData, const XML_Char* name);
  static void XMLCALL onText(void* userData, const XML_Char* s, int len);
  static void XMLCALL onDefault(void* userData, const XML_Char* s, int len);
  // Installed only so expat routes the CDATA delimiters here instead of to the
  // default handler, which would otherwise copy them into the item's text.
  static void XMLCALL onCdataStart(void* userData);
  static void XMLCALL onCdataEnd(void* userData);

  void startElement(const char* name, const XML_Char** atts);
  void endElement(const char* name);
  void characters(const char* data, int len);
  // Reads href/rel off an Atom <link>; RSS links arrive as element text.
  void captureAtomLink(const XML_Char** atts);
  void storeLink(NewsIndex::Record& record) const;
  void flushItem();

  XML_Parser parser_ = nullptr;
  HalFile feedFile_;
  HalFile indexFile_;
  HalFile bodyFile_;

  std::unique_ptr<char[]> bodyBuffer_;
  std::unique_ptr<NewsTextSink> sink_;

  uint32_t keepOrdinals_[NewsIndex::DAY_COUNT] = {};
  uint32_t syncOrdinal_ = 0;
  int32_t localOffsetSec_ = 0;

  bool inItem_ = false;
  // Depth inside the body element, so nested XHTML markup routes to the sink
  // instead of ending the field at the first child close tag.
  uint16_t bodyDepth_ = 0;
  Field field_ = Field::None;
  // content:encoded wins over description when a feed ships both.
  bool bodyIsEncoded_ = false;

  // Bigger than the stored title: Sky News wraps some headlines in an <a> whose
  // href alone is longer than the record, so the raw text has to survive intact
  // long enough for the sink to strip the markup off it.
  static constexpr size_t RAW_TITLE_BYTES = 512;
  char title_[RAW_TITLE_BYTES] = {};
  uint16_t titleLen_ = 0;
  // The item's own page URL. Longer than the stored field so a link that only
  // just overflows can still have its query stripped and fit.
  char link_[256] = {};
  uint16_t linkLen_ = 0;
  // An Atom <link href="..."> beats the element text of a later <link>.
  bool linkFromAttribute_ = false;
  char date_[48] = {};
  uint8_t dateLen_ = 0;
  char feedTitle_[64] = {};
  uint8_t feedTitleLen_ = 0;
  bool feedTitleDone_ = false;

  uint32_t bodyOffset_ = 0;
  uint16_t itemsKept_ = 0;
  uint16_t itemsSeen_ = 0;
  bool stopped_ = false;
  bool writeFailed_ = false;
};
