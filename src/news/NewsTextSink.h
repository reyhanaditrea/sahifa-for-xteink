#pragma once

#include <cstddef>
#include <cstdint>

// Turns a feed item's body into plain text for the article reader.
//
// Expat has already un-escaped the XML layer by the time text reaches us, so
// what arrives is HTML markup: "<p>Foo &amp; bar</p>" for an escaped
// description, or raw markup for a CDATA section. Both cases land here.
//
// The sink is a streaming state machine (expat hands text in arbitrary chunks,
// so a tag or an entity can straddle two calls) writing into a caller-owned
// buffer. Paragraphs are separated by a single '\n'; the reader splits on it.
// Pure C++ with no Arduino dependency so it can be unit-tested on the host.
class NewsTextSink {
 public:
  // buffer must stay alive for the sink's lifetime; capacity includes the
  // terminating NUL. Text past the cap is dropped and an ellipsis is appended.
  NewsTextSink(char* buffer, size_t capacity);

  void reset();

  // Character data from expat (not NUL-terminated).
  void addText(const char* data, size_t len);
  // Element boundaries for XHTML Atom content, where the markup arrives as real
  // elements rather than as text. Name must be the local name, lower-cased by
  // the caller when the feed shouts.
  void openElement(const char* localName);
  void closeElement(const char* localName);

  // Finishes the buffer (trailing whitespace trimmed, NUL written) and returns
  // the byte length, excluding the NUL.
  size_t finish();

  // Rolls the text back to an earlier length, dropping whatever was written
  // after it. The article extractor uses this to discard a paragraph once it
  // turns out to be too short to be prose. Any half-read tag or entity is
  // dropped with it, so the sink is left in a clean text state.
  void truncateTo(size_t length);

  size_t length() const { return length_; }
  bool truncated() const { return truncated_; }

 private:
  enum class State : uint8_t { Text, Tag, Entity, Comment };

  void putChar(char c);
  void putUtf8(uint32_t codepoint);
  void putRaw(const char* s, size_t len);
  void breakParagraph();
  void flushSeparator();
  void flushTag();
  void flushEntity();

  char* buffer_;
  size_t capacity_;
  size_t length_ = 0;
  bool truncated_ = false;

  State state_ = State::Text;
  // Tags and entities are bounded: anything longer is malformed and dropped.
  char pending_[40] = {};
  uint8_t pendingLen_ = 0;
  // Non-zero while inside <script>/<style>, whose text must never be emitted.
  uint8_t skipDepth_ = 0;
  bool pendingSpace_ = false;
  bool pendingBreak_ = false;
};
