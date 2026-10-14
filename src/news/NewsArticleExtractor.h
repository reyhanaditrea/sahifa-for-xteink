#pragma once

#include <cstddef>
#include <cstdint>

#include "news/NewsTextSink.h"

// Pulls the readable text out of a news article page.
//
// Feeds ship a one-sentence teaser (measured across the built-in outlets: 103
// to 289 characters), so the full story only exists on the publisher's page.
// Those pages are 130KB to 440KB of markup for a few kilobytes of prose and
// cannot be buffered on a C3, so this is a streaming scanner: chunks arrive as
// the socket delivers them and only the text worth reading is kept.
//
// The rule that survives contact with the real sites (checked 2026-08-30 against
// BBC, DW, NPR, CBC, France 24 and Al Jazeera):
//   * skip <script>/<style>/<nav>/<header>/<footer>/<aside>/<figure> subtrees;
//   * keep the text of <p>/<h2..h6>/<li>/<blockquote>;
//   * once an <article> opens, throw away everything collected before it and
//     stop at its close -- BBC and DW wrap the story in one, and the material
//     ahead of it is navigation. Pages without <article> (CBC, France 24) keep
//     the whole-page harvest, which the skip list already cleans up;
//   * drop paragraphs under 40 bytes: bylines, "Share", image credits.
//
// Pure C++ with no Arduino dependency so it can be unit-tested on the host.
class NewsArticleExtractor {
 public:
  // buffer must stay alive for the extractor's lifetime; capacity includes the
  // terminating NUL.
  NewsArticleExtractor(char* buffer, size_t capacity);

  // One chunk of page bytes, in arrival order. Tags, entities and paragraphs
  // may straddle chunk boundaries.
  void addChunk(const char* data, size_t len);

  // True once the story's </article> has been seen: the caller can abort the
  // transfer instead of downloading the page's footer and related links.
  bool done() const { return done_; }

  // Finishes the buffer and returns the text length, excluding the NUL.
  size_t finish();

  size_t length() const { return sink_.length(); }

  // Shortest text we treat as a real article. Below this the page was a consent
  // wall, a bot check, or rendered its body in JavaScript.
  static constexpr size_t MIN_ARTICLE_BYTES = 300;
  // Paragraphs shorter than this are furniture, not prose.
  static constexpr size_t MIN_PARAGRAPH_BYTES = 40;
  // Subheadings are held to a much lower bar: they are legitimately short.
  static constexpr size_t MIN_HEADING_BYTES = 8;

 private:
  enum class State : uint8_t { Text, Tag, Comment };

  void flushTag();
  void endParagraph();
  // Consumes one byte of a raw-text element, returning true once its closing
  // tag has been matched.
  bool matchRawClose(char c);

  NewsTextSink sink_;
  State state_ = State::Text;
  // Tag names are bounded; a longer one is not a tag we act on, and its tail is
  // dropped while the scanner keeps consuming to the '>'.
  char pending_[64] = {};
  uint8_t pendingLen_ = 0;

  // Depth inside a subtree whose text is never emitted. Only the tag that
  // opened the skip can nest it, so one name plus a counter is enough.
  char skipName_[12] = {};
  uint16_t skipDepth_ = 0;
  // <script> and <style> hold raw text, not markup: a '<' inside them belongs
  // to the code ("if (a<b)", document.write('<script ...>'), an escaped
  // "<\/script>"). Parsing those as tags left the skip depth stuck open and
  // swallowed whole pages, so their bodies are scanned for the literal close
  // tag instead. How many characters of it match so far:
  const char* rawClose_ = nullptr;
  uint8_t rawMatched_ = 0;

  uint16_t paragraphDepth_ = 0;
  bool paragraphIsHeading_ = false;
  // Where the open paragraph started, so a too-short one can be rolled back.
  size_t paragraphStart_ = 0;

  bool sawArticle_ = false;
  bool done_ = false;
};

namespace NewsArticleFetch {

enum class Result : uint8_t {
  Ok,           // text extracted
  Unreachable,  // the page could not be fetched at all
  NoText,       // fetched, but nothing readable came out (bot wall, JS-only)
};

// Streams url through NewsArticleExtractor into out. outLen receives the text
// length. Needs Wi-Fi up. Never allocates the page: at most a chunk at a time.
Result fetch(const char* url, char* out, size_t capacity, size_t& outLen);

// A page bigger than this is not an article we can afford to read; the biggest
// real one measured was 440KB.
inline constexpr size_t MAX_PAGE_BYTES = 768 * 1024;

}  // namespace NewsArticleFetch
