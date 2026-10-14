#include "news/NewsTextSink.h"

#include <cstring>

#include "Epub/htmlEntities.h"

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

// Block-level tags whose end (or, for the void ones, whose start) begins a new
// paragraph in the reader.
bool isBreakTag(const char* name, const size_t len) {
  static constexpr const char* TAGS[] = {"p",  "div", "br", "li", "tr", "h1",         "h2",   "h3",
                                         "h4", "h5",  "h6", "ul", "ol", "blockquote", "table"};
  for (const char* tag : TAGS) {
    if (nameEquals(name, len, tag)) return true;
  }
  return false;
}

bool isSkipTag(const char* name, const size_t len) {
  return nameEquals(name, len, "script") || nameEquals(name, len, "style");
}

}  // namespace

NewsTextSink::NewsTextSink(char* buffer, const size_t capacity) : buffer_(buffer), capacity_(capacity) {
  if (capacity_ > 0) buffer_[0] = '\0';
}

void NewsTextSink::reset() {
  length_ = 0;
  truncated_ = false;
  state_ = State::Text;
  pendingLen_ = 0;
  skipDepth_ = 0;
  pendingSpace_ = false;
  pendingBreak_ = false;
  if (capacity_ > 0) buffer_[0] = '\0';
}

void NewsTextSink::putChar(const char c) {
  // Leave room for the NUL and for the "..." the truncation marker appends.
  if (length_ + 1 >= capacity_) {
    truncated_ = true;
    return;
  }
  buffer_[length_++] = c;
}

void NewsTextSink::putRaw(const char* s, const size_t len) {
  for (size_t i = 0; i < len; i++) putChar(s[i]);
}

void NewsTextSink::putUtf8(const uint32_t codepoint) {
  if (codepoint < 0x80) {
    putChar(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    putChar(static_cast<char>(0xC0 | (codepoint >> 6)));
    putChar(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    putChar(static_cast<char>(0xE0 | (codepoint >> 12)));
    putChar(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    putChar(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0x10FFFF) {
    putChar(static_cast<char>(0xF0 | (codepoint >> 18)));
    putChar(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    putChar(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    putChar(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

void NewsTextSink::breakParagraph() {
  if (length_ == 0) return;  // no leading blank paragraph
  pendingSpace_ = false;
  pendingBreak_ = true;
}

// Applies the deferred separator right before real text lands, so runs of
// markup and whitespace collapse into at most one space or one break.
void NewsTextSink::flushSeparator() {
  if (pendingBreak_) {
    putChar('\n');
    pendingBreak_ = false;
    pendingSpace_ = false;
  } else if (pendingSpace_) {
    if (length_ > 0) putChar(' ');
    pendingSpace_ = false;
  }
}

void NewsTextSink::flushTag() {
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

  if (isSkipTag(p, nameLen)) {
    if (closing) {
      if (skipDepth_ > 0) skipDepth_--;
    } else {
      skipDepth_++;
    }
    return;
  }
  if (skipDepth_ > 0) return;
  if (isBreakTag(p, nameLen)) breakParagraph();
}

void NewsTextSink::flushEntity() {
  if (skipDepth_ > 0) return;
  // pending_ holds the entity body without '&' and ';'.
  if (pendingLen_ == 0) return;
  flushSeparator();
  if (pending_[0] == '#') {
    uint32_t value = 0;
    if (pendingLen_ > 1 && (pending_[1] == 'x' || pending_[1] == 'X')) {
      for (uint8_t i = 2; i < pendingLen_; i++) {
        const char c = lower(pending_[i]);
        if (c >= '0' && c <= '9') {
          value = value * 16 + static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
          value = value * 16 + static_cast<uint32_t>(c - 'a' + 10);
        } else {
          return;
        }
      }
    } else {
      for (uint8_t i = 1; i < pendingLen_; i++) {
        if (pending_[i] < '0' || pending_[i] > '9') return;
        value = value * 10 + static_cast<uint32_t>(pending_[i] - '0');
      }
    }
    // nbsp and friends read as ordinary spaces on a 1-bit screen.
    if (value == 0xA0 || value == 0x2007 || value == 0x202F) {
      putChar(' ');
      return;
    }
    putUtf8(value);
    return;
  }

  // htmlEntities.h wants the full "&name;" form.
  char full[sizeof(pending_) + 2];
  full[0] = '&';
  memcpy(full + 1, pending_, pendingLen_);
  full[pendingLen_ + 1] = ';';
  const size_t fullLen = static_cast<size_t>(pendingLen_) + 2;
  if (const char* utf8 = lookupHtmlEntity(full, fullLen)) {
    // A non-breaking space is a space here; keeping U+00A0 would defeat the
    // line breaker in the reader.
    if (strcmp(utf8, "\xC2\xA0") == 0) {
      putChar(' ');
    } else {
      putRaw(utf8, strlen(utf8));
    }
    return;
  }
  // Unknown entity: emit it verbatim rather than swallowing the text.
  putRaw(full, fullLen);
}

void NewsTextSink::addText(const char* data, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = data[i];
    switch (state_) {
      case State::Text:
        if (c == '<') {
          state_ = State::Tag;
          pendingLen_ = 0;
        } else if (c == '&') {
          state_ = State::Entity;
          pendingLen_ = 0;
        } else if (isSpace(c)) {
          if (skipDepth_ == 0 && length_ > 0 && !pendingBreak_) pendingSpace_ = true;
        } else if (skipDepth_ == 0) {
          flushSeparator();
          putChar(c);
        }
        break;

      case State::Tag:
        // "<!--" starts a comment; "<!" anything else (doctype) ends at '>'.
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
        } else if (pendingLen_ < sizeof(pending_)) {
          pending_[pendingLen_++] = c;
        }
        // A tag longer than pending_ keeps consuming until '>' with its tail
        // dropped; the name is already captured in the first bytes.
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

      case State::Entity:
        if (c == ';') {
          flushEntity();
          state_ = State::Text;
          pendingLen_ = 0;
        } else if (isSpace(c) || c == '<' || c == '&' || pendingLen_ >= sizeof(pending_)) {
          // Malformed: a bare '&' in running text. Emit it and re-read c.
          if (skipDepth_ == 0) {
            flushSeparator();
            putChar('&');
            putRaw(pending_, pendingLen_);
          }
          state_ = State::Text;
          pendingLen_ = 0;
          i--;
        } else {
          pending_[pendingLen_++] = c;
        }
        break;
    }
  }
}

void NewsTextSink::openElement(const char* localName) {
  const size_t len = strlen(localName);
  if (isSkipTag(localName, len)) {
    skipDepth_++;
    return;
  }
  if (skipDepth_ > 0) return;
  if (nameEquals(localName, len, "br")) breakParagraph();
}

void NewsTextSink::closeElement(const char* localName) {
  const size_t len = strlen(localName);
  if (isSkipTag(localName, len)) {
    if (skipDepth_ > 0) skipDepth_--;
    return;
  }
  if (skipDepth_ > 0) return;
  if (isBreakTag(localName, len)) breakParagraph();
}

void NewsTextSink::truncateTo(const size_t length) {
  if (length >= length_) return;
  length_ = length;
  state_ = State::Text;
  pendingLen_ = 0;
  pendingSpace_ = false;
  pendingBreak_ = false;
  if (length_ < capacity_) buffer_[length_] = '\0';
}

size_t NewsTextSink::finish() {
  // An entity or tag left open at the end of the stream is malformed markup;
  // drop it rather than emitting half a token.
  state_ = State::Text;
  pendingLen_ = 0;
  while (length_ > 0 && (isSpace(buffer_[length_ - 1]))) length_--;
  if (truncated_) {
    static constexpr char ELLIPSIS[] = "\xE2\x80\xA6";  // U+2026
    // Trim to fit the marker plus the NUL; the buffer is never smaller than
    // that in practice, but a tiny buffer must still terminate cleanly.
    while (length_ > 0 && length_ + sizeof(ELLIPSIS) > capacity_) length_--;
    // Never split a UTF-8 sequence: back off continuation bytes first.
    while (length_ > 0 && (static_cast<unsigned char>(buffer_[length_ - 1]) & 0xC0) == 0x80) length_--;
    if (length_ > 0 && (static_cast<unsigned char>(buffer_[length_ - 1]) & 0x80) != 0) length_--;
    if (length_ + sizeof(ELLIPSIS) <= capacity_) {
      memcpy(buffer_ + length_, ELLIPSIS, sizeof(ELLIPSIS) - 1);
      length_ += sizeof(ELLIPSIS) - 1;
    }
  }
  if (length_ < capacity_) buffer_[length_] = '\0';
  return length_;
}
