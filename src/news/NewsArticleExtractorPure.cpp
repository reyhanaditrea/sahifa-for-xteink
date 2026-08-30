#include <cstring>

#include "news/NewsArticleExtractor.h"

namespace {

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

char lower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c | 0x20) : c; }

bool nameEquals(const char* name, const size_t len, const char* literal) {
  const size_t litLen = strlen(literal);
  if (len != litLen) return false;
  for (size_t i = 0; i < len; i++) {
    if (lower(name[i]) != literal[i]) return false;
  }
  return true;
}

// Subtrees whose text is never part of the story. <form> and <button> catch the
// newsletter and consent widgets that sit inside the article on several sites.
bool isSkipTag(const char* name, const size_t len) {
  static constexpr const char* TAGS[] = {"script", "style", "noscript", "nav",    "header", "footer", "aside",
                                         "figure", "form",  "button",   "select", "svg",    "iframe", "template"};
  for (const char* tag : TAGS) {
    if (nameEquals(name, len, tag)) return true;
  }
  return false;
}

// Elements whose content is text, not markup. Their end tag is the only thing
// the scanner may look for inside them.
const char* rawCloseTag(const char* name, const size_t len) {
  if (nameEquals(name, len, "script")) return "</script";
  if (nameEquals(name, len, "style")) return "</style";
  return nullptr;
}

bool isHeadingTag(const char* name, const size_t len) {
  static constexpr const char* TAGS[] = {"h2", "h3", "h4", "h5", "h6"};
  for (const char* tag : TAGS) {
    if (nameEquals(name, len, tag)) return true;
  }
  return false;
}

// Elements whose text is prose. <h1> is left out on purpose: it repeats the
// headline the reader already shows above the body.
bool isParagraphTag(const char* name, const size_t len) {
  static constexpr const char* TAGS[] = {"p", "h2", "h3", "h4", "h5", "h6", "li", "blockquote", "dd"};
  for (const char* tag : TAGS) {
    if (nameEquals(name, len, tag)) return true;
  }
  return false;
}

}  // namespace

NewsArticleExtractor::NewsArticleExtractor(char* buffer, const size_t capacity) : sink_(buffer, capacity) {}

void NewsArticleExtractor::endParagraph() {
  if (paragraphDepth_ == 0) return;
  paragraphDepth_--;
  if (paragraphDepth_ > 0) return;
  // A subheading is allowed to be short -- "What we know so far" is real text.
  // Everything else has to clear the furniture bar: bylines, share prompts and
  // image credits all sit under it.
  const size_t minimum = paragraphIsHeading_ ? MIN_HEADING_BYTES : MIN_PARAGRAPH_BYTES;
  paragraphIsHeading_ = false;
  if (sink_.length() - paragraphStart_ < minimum) {
    sink_.truncateTo(paragraphStart_);
    // truncateTo drops the paragraph break the previous paragraph had armed but
    // not yet written (the sink defers separators until real text follows), so
    // re-arm it: the next paragraph must still start on its own line.
    if (paragraphStart_ > 0) sink_.closeElement("p");
    return;
  }
  sink_.closeElement("p");
}

void NewsArticleExtractor::flushTag() {
  // pending_ holds the tag body without '<' and '>'.
  const char* p = pending_;
  size_t len = pendingLen_;
  const bool closing = (len > 0 && p[0] == '/');
  if (closing) {
    p++;
    len--;
  }
  size_t nameLen = 0;
  while (nameLen < len && !isSpace(p[nameLen]) && p[nameLen] != '/') nameLen++;
  if (nameLen == 0) return;

  if (skipDepth_ > 0) {
    // Inside a skipped subtree only its own tag name changes the depth, so
    // <script> inside <nav> cannot close the nav early.
    if (nameEquals(p, nameLen, skipName_)) {
      if (closing) {
        skipDepth_--;
      } else if (p[len - 1] != '/') {
        skipDepth_++;
      }
    }
    return;
  }

  if (!closing && isSkipTag(p, nameLen)) {
    // A self-closing skip element has no subtree to skip.
    if (len > 0 && p[len - 1] == '/') return;
    rawClose_ = rawCloseTag(p, nameLen);
    rawMatched_ = 0;
    const size_t copy = (nameLen < sizeof(skipName_) - 1) ? nameLen : sizeof(skipName_) - 1;
    for (size_t i = 0; i < copy; i++) skipName_[i] = lower(p[i]);
    skipName_[copy] = '\0';
    skipDepth_ = 1;
    // An open paragraph cannot survive a skipped subtree opening inside it.
    if (paragraphDepth_ > 0) endParagraph();
    return;
  }

  if (nameEquals(p, nameLen, "article")) {
    if (closing) {
      if (sawArticle_) {
        endParagraph();
        done_ = true;
      }
      return;
    }
    if (!sawArticle_) {
      // Everything ahead of the story was navigation and teasers.
      sawArticle_ = true;
      sink_.reset();
      paragraphDepth_ = 0;
      paragraphStart_ = 0;
    }
    return;
  }

  if (nameEquals(p, nameLen, "br")) {
    if (paragraphDepth_ > 0) sink_.openElement("br");
    return;
  }

  if (!isParagraphTag(p, nameLen)) return;
  if (closing) {
    endParagraph();
    return;
  }
  if (paragraphDepth_ == 0) {
    paragraphStart_ = sink_.length();
    paragraphIsHeading_ = isHeadingTag(p, nameLen);
  }
  paragraphDepth_++;
}

void NewsArticleExtractor::addChunk(const char* data, const size_t len) {
  for (size_t i = 0; i < len && !done_; i++) {
    const char c = data[i];
    switch (state_) {
      case State::Text:
        if (rawClose_ != nullptr) {
          if (matchRawClose(c)) {
            // The name is already consumed; the rest of the tag is dropped by
            // flushTag, which ignores an empty name.
            state_ = State::Tag;
            pendingLen_ = 0;
          }
          break;
        }
        if (c == '<') {
          state_ = State::Tag;
          pendingLen_ = 0;
        } else if (skipDepth_ == 0 && paragraphDepth_ > 0) {
          // The sink owns entity decoding and whitespace collapsing; the run
          // handed to it never contains markup, since this loop splits on '<'.
          sink_.addText(&c, 1);
        }
        break;

      case State::Tag:
        if (pendingLen_ == 3 && pending_[0] == '!' && pending_[1] == '-' && pending_[2] == '-') {
          state_ = State::Comment;
          pendingLen_ = 0;
          i--;  // re-read this char as comment content
          break;
        }
        if (c == '>') {
          flushTag();
          state_ = State::Text;
          pendingLen_ = 0;
        } else if (pendingLen_ < sizeof(pending_) - 1) {
          pending_[pendingLen_++] = c;
          pending_[pendingLen_] = '\0';
        }
        break;

      case State::Comment:
        // Ends at "-->".
        if (c == '>' && pendingLen_ >= 2) {
          state_ = State::Text;
          pendingLen_ = 0;
        } else if (c == '-') {
          if (pendingLen_ < 2) pendingLen_++;
        } else {
          pendingLen_ = 0;
        }
        break;
    }
  }
}

bool NewsArticleExtractor::matchRawClose(const char c) {
  if (lower(c) == rawClose_[rawMatched_]) {
    rawMatched_++;
    if (rawClose_[rawMatched_] == '\0') {
      rawClose_ = nullptr;
      rawMatched_ = 0;
      skipDepth_ = 0;
      return true;
    }
    return false;
  }
  // Restart the match, allowing this character to be the '<' of the real one.
  rawMatched_ = (c == '<') ? 1 : 0;
  return false;
}

size_t NewsArticleExtractor::finish() {
  // A paragraph still open at the end of the stream is a truncated page; keep
  // what it holds if it is long enough to be prose.
  if (paragraphDepth_ > 0) {
    paragraphDepth_ = 1;
    endParagraph();
  }
  return sink_.finish();
}
