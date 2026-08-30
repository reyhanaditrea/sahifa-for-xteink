#include "news/NewsFeedParser.h"

#include <Logging.h>
#include <Memory.h>
#include <XmlParserUtils.h>

#include <cstring>

#include "news/NewsDate.h"

namespace {

constexpr size_t PARSE_BUFFER_SIZE = 1024;
// Largest article body we keep. Feeds ship summaries; the few that ship full
// text (content:encoded) get cut with an ellipsis rather than eating the heap
// the parser still needs.
constexpr size_t BODY_BUFFER_SIZE = 8192;

bool isItemElement(const char* local) { return strcmp(local, "item") == 0 || strcmp(local, "entry") == 0; }

// Cuts a title on a UTF-8 boundary so a multi-byte character is never split.
uint8_t safeUtf8Cut(const char* s, uint8_t len, const uint8_t max) {
  if (len <= max) return len;
  len = max;
  while (len > 0 && (static_cast<unsigned char>(s[len]) & 0xC0) == 0x80) len--;
  return len;
}

}  // namespace

NewsFeedParser::NewsFeedParser() = default;

NewsFeedParser::~NewsFeedParser() { abort(); }

bool NewsFeedParser::begin(const char* feedPath, const char* indexPath, const char* bodyPath,
                           const uint32_t keepOrdinals[NewsIndex::DAY_COUNT], const uint32_t syncOrdinal,
                           const int32_t localOffsetSec) {
  memcpy(keepOrdinals_, keepOrdinals, sizeof(keepOrdinals_));
  syncOrdinal_ = syncOrdinal;
  localOffsetSec_ = localOffsetSec;

  bodyBuffer_ = makeUniqueNoThrow<char[]>(BODY_BUFFER_SIZE);
  if (!bodyBuffer_) {
    LOG_ERR("NEWS", "OOM: %zu bytes for body buffer", BODY_BUFFER_SIZE);
    return false;
  }
  sink_ = makeUniqueNoThrow<NewsTextSink>(bodyBuffer_.get(), BODY_BUFFER_SIZE);
  if (!sink_) {
    LOG_ERR("NEWS", "OOM: NewsTextSink");
    bodyBuffer_.reset();
    return false;
  }

  if (!Storage.openFileForRead("NEWS", feedPath, feedFile_)) {
    bodyBuffer_.reset();
    sink_.reset();
    return false;
  }
  if (!Storage.openFileForWrite("NEWS", indexPath, indexFile_) ||
      !Storage.openFileForWrite("NEWS", bodyPath, bodyFile_)) {
    abort();
    return false;
  }

  // The header is rewritten with the real count in finish(); reserve its space
  // now so record offsets are final as they are written.
  const NewsIndex::Header placeholder{NewsIndex::MAGIC, NewsIndex::VERSION, 0, syncOrdinal_};
  if (indexFile_.write(&placeholder, sizeof(placeholder)) != sizeof(placeholder)) {
    LOG_ERR("NEWS", "Failed to write index header");
    abort();
    return false;
  }

  parser_ = XML_ParserCreate(nullptr);
  if (!parser_) {
    LOG_ERR("NEWS", "Couldn't allocate XML parser");
    abort();
    return false;
  }
  XML_SetUserData(parser_, this);
  XML_SetElementHandler(parser_, onStart, onEnd);
  XML_SetCharacterDataHandler(parser_, onText);
  // Feeds routinely reference HTML entities that XML does not define; expanding
  // them through the default handler keeps the text rather than erroring out.
  XML_SetDefaultHandlerExpand(parser_, onDefault);
  // With a default handler installed and no CDATA handlers, expat reports the
  // "<![CDATA[" and "]]>" delimiters themselves to the default handler. BBC (and
  // most CMS feeds) wrap every title and description in CDATA, so without these
  // no-op handlers the delimiters land in the title text and the body's opening
  // "<![CDATA[" is swallowed as if it were an HTML tag, emptying the article.
  XML_SetCdataSectionHandler(parser_, onCdataStart, onCdataEnd);
  return true;
}

NewsFeedParser::Step NewsFeedParser::step() {
  if (!parser_) return Step::Error;
  if (stopped_) return Step::Done;

  void* const buf = XML_GetBuffer(parser_, PARSE_BUFFER_SIZE);
  if (!buf) {
    LOG_ERR("NEWS", "Couldn't allocate parse buffer");
    return Step::Error;
  }
  const size_t len = feedFile_.read(buf, PARSE_BUFFER_SIZE);
  if (len == 0 && feedFile_.available() > 0) {
    LOG_ERR("NEWS", "Feed file read error");
    return Step::Error;
  }
  const int done = feedFile_.available() == 0;

  if (XML_ParseBuffer(parser_, static_cast<int>(len), done) == XML_STATUS_ERROR) {
    // Hitting the item cap stops the parser deliberately; everything written so
    // far is valid, so that is a normal finish rather than an error.
    if (stopped_) return Step::Done;
    LOG_ERR("NEWS", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser_),
            XML_ErrorString(XML_GetErrorCode(parser_)));
    // Items already written stay usable; the caller decides whether a partial
    // feed is worth keeping.
    return itemsKept_ > 0 ? Step::Done : Step::Error;
  }
  if (writeFailed_) {
    LOG_ERR("NEWS", "SD write failed while parsing");
    return Step::Error;
  }
  return done ? Step::Done : Step::More;
}

void NewsFeedParser::startElement(const char* name, const XML_Char** atts) {
  const char* local = xmlLocalName(name);

  if (inItem_ && field_ == Field::Body) {
    // Inside an Atom xhtml body: markup arrives as real elements.
    bodyDepth_++;
    sink_->openElement(local);
    return;
  }

  if (isItemElement(local)) {
    inItem_ = true;
    field_ = Field::None;
    titleLen_ = 0;
    dateLen_ = 0;
    linkLen_ = 0;
    linkFromAttribute_ = false;
    bodyIsEncoded_ = false;
    sink_->reset();
    feedTitleDone_ = true;  // any <title> from here on belongs to an item
    return;
  }

  if (!inItem_) {
    // The channel's own title, used as the display name for user feeds.
    if (!feedTitleDone_ && strcmp(local, "title") == 0) {
      field_ = Field::FeedTitle;
      feedTitleLen_ = 0;
    }
    return;
  }

  if (strcmp(local, "title") == 0) {
    field_ = Field::Title;
    titleLen_ = 0;
  } else if (strcmp(local, "pubDate") == 0 || strcmp(local, "date") == 0 || strcmp(local, "updated") == 0 ||
             strcmp(local, "published") == 0 || strcmp(local, "modified") == 0) {
    // dc:date arrives as local name "date". Prefer the first date we see:
    // Atom entries carry both published and updated, and published is first.
    if (dateLen_ == 0) {
      field_ = Field::Date;
    }
  } else if (strcmp(local, "encoded") == 0) {
    // content:encoded -- full text, always preferred over description.
    field_ = Field::Body;
    bodyIsEncoded_ = true;
    bodyDepth_ = 0;
    sink_->reset();
  } else if (strcmp(local, "description") == 0 || strcmp(local, "summary") == 0 || strcmp(local, "content") == 0) {
    if (!bodyIsEncoded_) {
      field_ = Field::Body;
      bodyDepth_ = 0;
      sink_->reset();
    }
  } else if (strcmp(local, "link") == 0) {
    // The item's own page, kept so the reader can fetch the full article.
    // Atom carries it in href, RSS in the element text; an Atom feed also
    // ships <link rel="replies"> and friends, which are not the article.
    field_ = Field::Link;
    if (atts) captureAtomLink(atts);
  }
}

void NewsFeedParser::endElement(const char* name) {
  const char* local = xmlLocalName(name);

  if (inItem_ && field_ == Field::Body && bodyDepth_ > 0) {
    bodyDepth_--;
    sink_->closeElement(local);
    return;
  }

  if (isItemElement(local)) {
    flushItem();
    inItem_ = false;
    field_ = Field::None;
    return;
  }
  if (field_ != Field::None) field_ = Field::None;
}

void NewsFeedParser::characters(const char* data, const int len) {
  if (len <= 0) return;
  switch (field_) {
    case Field::Title: {
      const size_t room = sizeof(title_) - 1 - titleLen_;
      if (room == 0) return;
      const size_t take = (static_cast<size_t>(len) < room) ? static_cast<size_t>(len) : room;
      memcpy(title_ + titleLen_, data, take);
      titleLen_ = static_cast<uint16_t>(titleLen_ + take);
      title_[titleLen_] = '\0';
      break;
    }
    case Field::FeedTitle: {
      const size_t room = sizeof(feedTitle_) - 1 - feedTitleLen_;
      if (room == 0) return;
      const size_t take = (static_cast<size_t>(len) < room) ? static_cast<size_t>(len) : room;
      memcpy(feedTitle_ + feedTitleLen_, data, take);
      feedTitleLen_ = static_cast<uint8_t>(feedTitleLen_ + take);
      feedTitle_[feedTitleLen_] = '\0';
      break;
    }
    case Field::Date: {
      const size_t room = sizeof(date_) - 1 - dateLen_;
      if (room == 0) return;
      const size_t take = (static_cast<size_t>(len) < room) ? static_cast<size_t>(len) : room;
      memcpy(date_ + dateLen_, data, take);
      dateLen_ = static_cast<uint8_t>(dateLen_ + take);
      date_[dateLen_] = '\0';
      break;
    }
    case Field::Body:
      sink_->addText(data, static_cast<size_t>(len));
      break;
    case Field::Link: {
      if (linkFromAttribute_) return;
      const size_t room = sizeof(link_) - 1 - linkLen_;
      if (room == 0) return;
      const size_t take = (static_cast<size_t>(len) < room) ? static_cast<size_t>(len) : room;
      memcpy(link_ + linkLen_, data, take);
      linkLen_ = static_cast<uint16_t>(linkLen_ + take);
      link_[linkLen_] = '\0';
      break;
    }
    case Field::None:
      break;
  }
}

void NewsFeedParser::captureAtomLink(const XML_Char** atts) {
  const char* href = nullptr;
  const char* rel = nullptr;
  for (size_t i = 0; atts[i] && atts[i + 1]; i += 2) {
    const char* name = xmlLocalName(atts[i]);
    if (strcmp(name, "href") == 0) {
      href = atts[i + 1];
    } else if (strcmp(name, "rel") == 0) {
      rel = atts[i + 1];
    }
  }
  if (!href || href[0] == '\0') return;
  // rel absent means alternate, which is the article itself; "self", "replies"
  // and "enclosure" point somewhere else.
  if (rel && rel[0] != '\0' && strcmp(rel, "alternate") != 0) return;
  const size_t len = strlen(href);
  if (len >= sizeof(link_)) return;
  memcpy(link_, href, len);
  link_[len] = '\0';
  linkLen_ = static_cast<uint16_t>(len);
  linkFromAttribute_ = true;
}

// Copies the item link into the record, if it is a web URL that fits. A link
// that does not fit simply means no full-text option for that item, which is
// better than storing a truncated URL that would 404.
void NewsFeedParser::storeLink(NewsIndex::Record& record) const {
  size_t start = 0;
  size_t end = linkLen_;
  while (start < end && (link_[start] == ' ' || link_[start] == '\n' || link_[start] == '\r' || link_[start] == '\t')) {
    start++;
  }
  while (end > start &&
         (link_[end - 1] == ' ' || link_[end - 1] == '\n' || link_[end - 1] == '\r' || link_[end - 1] == '\t')) {
    end--;
  }
  size_t len = end - start;
  const char* url = link_ + start;
  if (len < 8 || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) return;
  if (len >= NewsIndex::URL_BYTES) {
    // Feeds tag their links with campaign parameters; without the query most
    // still fit, and every publisher tested serves the same page either way.
    const char* query = static_cast<const char*>(memchr(url, '?', len));
    if (!query) return;
    len = static_cast<size_t>(query - url);
    if (len == 0 || len >= NewsIndex::URL_BYTES) return;
  }
  memcpy(record.url, url, len);
  record.url[len] = '\0';
}

void NewsFeedParser::flushItem() {
  itemsSeen_++;

  // Titles carry markup too: Sky News ends several of its headlines with an
  // empty "<a/>", and CMS feeds leave <b>/<i> in. Run the title through the
  // same sink the body uses so tags and entities are resolved rather than
  // shown, then flatten any paragraph break the sink emits -- a title is one
  // line.
  char clean[NewsIndex::TITLE_BYTES];
  NewsTextSink titleSink(clean, sizeof(clean));
  titleSink.addText(title_, titleLen_);
  const size_t cleanLen = titleSink.finish();
  for (size_t i = 0; i < cleanLen; i++) {
    if (clean[i] == '\n') clean[i] = ' ';
  }

  // Trim the title: feeds pad it with the whitespace of their own indentation.
  size_t start = 0;
  while (start < cleanLen &&
         (clean[start] == ' ' || clean[start] == '\n' || clean[start] == '\r' || clean[start] == '\t')) {
    start++;
  }
  size_t end = cleanLen;
  while (end > start &&
         (clean[end - 1] == ' ' || clean[end - 1] == '\n' || clean[end - 1] == '\r' || clean[end - 1] == '\t')) {
    end--;
  }
  if (end == start) return;  // an item with no title has nothing to show

  const uint32_t epoch = NewsDate::parseFeedDateToEpoch(date_);
  const uint32_t ordinal = NewsDate::localOrdinalForEpoch(epoch, localOffsetSec_);
  if (ordinal != 0 && keepOrdinals_[0] != 0) {
    bool wanted = false;
    for (const uint32_t keep : keepOrdinals_) {
      if (keep != 0 && keep == ordinal) {
        wanted = true;
        break;
      }
    }
    if (!wanted) return;
  }

  const size_t bodyLen = sink_->finish();

  NewsIndex::Record record{};
  const uint8_t titleBytes =
      safeUtf8Cut(clean + start, static_cast<uint8_t>(end - start), static_cast<uint8_t>(NewsIndex::TITLE_BYTES - 1));
  memcpy(record.title, clean + start, titleBytes);
  record.title[titleBytes] = '\0';
  record.epoch = epoch;
  record.dayOrdinal = ordinal;
  record.bodyOffset = bodyOffset_;
  record.bodyLen = static_cast<uint32_t>(bodyLen);
  storeLink(record);

  if (bodyLen > 0 && bodyFile_.write(bodyBuffer_.get(), bodyLen) != bodyLen) {
    writeFailed_ = true;
    return;
  }
  if (indexFile_.write(&record, sizeof(record)) != sizeof(record)) {
    writeFailed_ = true;
    return;
  }
  bodyOffset_ += static_cast<uint32_t>(bodyLen);
  itemsKept_++;

  if (itemsKept_ >= NewsIndex::MAX_ITEMS_PER_FEED) {
    stopped_ = true;
    XML_StopParser(parser_, XML_FALSE);
  }
}

bool NewsFeedParser::finish(Result& result) {
  if (parser_) {
    destroyXmlParser(parser_);
    parser_ = nullptr;
  }
  feedFile_.close();
  bodyFile_.close();

  const NewsIndex::Header header{NewsIndex::MAGIC, NewsIndex::VERSION, itemsKept_, syncOrdinal_};
  bool ok = !writeFailed_;
  if (ok && (!indexFile_.seek(0) || indexFile_.write(&header, sizeof(header)) != sizeof(header))) {
    LOG_ERR("NEWS", "Failed to rewrite index header");
    ok = false;
  }
  indexFile_.close();

  result.itemsKept = itemsKept_;
  result.itemsSeen = itemsSeen_;
  sink_.reset();
  bodyBuffer_.reset();
  return ok;
}

void NewsFeedParser::abort() {
  if (parser_) {
    destroyXmlParser(parser_);
    parser_ = nullptr;
  }
  if (feedFile_.isOpen()) feedFile_.close();
  if (indexFile_.isOpen()) indexFile_.close();
  if (bodyFile_.isOpen()) bodyFile_.close();
  sink_.reset();
  bodyBuffer_.reset();
}

void XMLCALL NewsFeedParser::onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  static_cast<NewsFeedParser*>(userData)->startElement(name, atts);
}

void XMLCALL NewsFeedParser::onEnd(void* userData, const XML_Char* name) {
  static_cast<NewsFeedParser*>(userData)->endElement(name);
}

void XMLCALL NewsFeedParser::onText(void* userData, const XML_Char* s, const int len) {
  static_cast<NewsFeedParser*>(userData)->characters(s, len);
}

void XMLCALL NewsFeedParser::onDefault(void* userData, const XML_Char* s, const int len) {
  // Undefined entities (&nbsp; and friends) reach the default handler as raw
  // text; the sink decodes them the same way it decodes them inside CDATA.
  // Comments, processing instructions and the DOCTYPE arrive here too, so
  // anything that opens like markup is dropped rather than pushed into a field.
  if (len <= 0 || s[0] == '<') return;
  static_cast<NewsFeedParser*>(userData)->characters(s, len);
}

void XMLCALL NewsFeedParser::onCdataStart(void*) {}

void XMLCALL NewsFeedParser::onCdataEnd(void*) {}
