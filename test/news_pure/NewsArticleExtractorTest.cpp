#include <gtest/gtest.h>

#include <string>

#include "news/NewsArticleExtractor.h"

namespace {

// Feeds the page in one go.
std::string extract(const std::string& html, const size_t capacity = 4096) {
  std::string buffer(capacity, '\0');
  NewsArticleExtractor extractor(buffer.data(), capacity);
  extractor.addChunk(html.data(), html.size());
  const size_t len = extractor.finish();
  return buffer.substr(0, len);
}

// Feeds the page in fixed-size chunks, the way the socket delivers it.
std::string extractChunked(const std::string& html, const size_t chunk, const size_t capacity = 4096) {
  std::string buffer(capacity, '\0');
  NewsArticleExtractor extractor(buffer.data(), capacity);
  for (size_t i = 0; i < html.size() && !extractor.done(); i += chunk) {
    extractor.addChunk(html.data() + i, std::min(chunk, html.size() - i));
  }
  const size_t len = extractor.finish();
  return buffer.substr(0, len);
}

// 60 characters: past MIN_PARAGRAPH_BYTES, so it survives the furniture filter.
constexpr const char* PARA_A = "The dramatic footage was almost impossible to miss at all.";
constexpr const char* PARA_B = "Rescuers continued to search for the people still missing.";

TEST(NewsArticleExtractor, KeepsParagraphsAndDropsChrome) {
  const std::string html = std::string("<html><body><nav><p>Home News Sport Weather iPlayer Sounds</p></nav>") +
                           "<script>var x = \"<p>not text at all, just a string</p>\";</script>" + "<p>" + PARA_A +
                           "</p>" + "<p>Share this article</p>" +  // under 40 bytes: furniture
                           "<p>" + PARA_B + "</p></body></html>";
  EXPECT_EQ(extract(html), std::string(PARA_A) + "\n" + PARA_B);
}

TEST(NewsArticleExtractor, ArticleResetsWhatCameBefore) {
  // BBC and DW wrap the story in <article>; everything ahead of it is teasers.
  const std::string html = std::string("<p>Sign up for our newsletter to get the morning briefing.</p>") +
                           "<article><p>" + PARA_A + "</p></article>" + "<p>More from the world section right here</p>";
  EXPECT_EQ(extract(html), PARA_A);
}

TEST(NewsArticleExtractor, StopsAtArticleClose) {
  const std::string html = std::string("<article><p>") + PARA_A + "</p></article><p>" + PARA_B + "</p>";
  std::string buffer(4096, '\0');
  NewsArticleExtractor extractor(buffer.data(), buffer.size());
  extractor.addChunk(html.data(), html.size());
  EXPECT_TRUE(extractor.done());
  const size_t len = extractor.finish();
  EXPECT_EQ(buffer.substr(0, len), PARA_A);
}

TEST(NewsArticleExtractor, PagesWithoutArticleKeepEverything) {
  // CBC and France 24 ship no <article>; the skip list is what cleans them up.
  const std::string html = std::string("<header><p>Accessibility links skip to main content now</p></header>") + "<p>" +
                           PARA_A + "</p><footer><p>Copyright and terms of use for this website</p></footer>";
  EXPECT_EQ(extract(html), PARA_A);
}

TEST(NewsArticleExtractor, SurvivesChunkBoundaries) {
  const std::string html = std::string("<article><p>") + PARA_A + "</p><p>" + PARA_B + "</p></article>";
  const std::string expected = std::string(PARA_A) + "\n" + PARA_B;
  // 1-byte chunks split every tag and entity; 7 and 64 land mid-name.
  EXPECT_EQ(extractChunked(html, 1), expected);
  EXPECT_EQ(extractChunked(html, 7), expected);
  EXPECT_EQ(extractChunked(html, 64), expected);
}

TEST(NewsArticleExtractor, DecodesEntitiesAndInlineMarkup) {
  const std::string html =
      "<p>Ministers said &quot;the &amp; in R&amp;D&quot; would <b>not</b> change &#8212; nor would the rest.</p>";
  EXPECT_EQ(extract(html), "Ministers said \"the & in R&D\" would not change \xE2\x80\x94 nor would the rest.");
}

TEST(NewsArticleExtractor, NestedSkipTagsDoNotEndEarly) {
  // A <script> inside <nav> must not close the nav and let its links through.
  const std::string html = std::string("<nav><script>var a = 1;</script><p>Home News Sport Weather Sounds</p></nav>") +
                           "<p>" + PARA_A + "</p>";
  EXPECT_EQ(extract(html), PARA_A);
}

TEST(NewsArticleExtractor, ScriptBodiesAreRawText) {
  // What actually broke NPR and France 24: markup inside script strings. Parsed
  // as tags, the nested "<script" bumped the skip depth and the real </script>
  // never brought it back down, so the whole page came out empty.
  const std::string html = std::string("<script>var a = 1; if (a<2) { document.write('<script src=\"x.js\">'); }") +
                           "var s = \"<\\/script>\";</script><p>" + PARA_A + "</p>";
  EXPECT_EQ(extract(html), PARA_A);
  // Chunked the same way the socket delivers it, so the close tag straddles.
  EXPECT_EQ(extractChunked(html, 3), PARA_A);
}

TEST(NewsArticleExtractor, StyleBodiesAreRawText) {
  const std::string html = std::string("<style>.a > .b { content: \"<p>\"; }</style><p>") + PARA_A + "</p>";
  EXPECT_EQ(extract(html), PARA_A);
}

TEST(NewsArticleExtractor, HeadingsAndListsCount) {
  const std::string html =
      std::string("<h2>What we know so far about the flooding</h2><ul><li>") + PARA_B + "</li></ul>";
  EXPECT_EQ(extract(html), "What we know so far about the flooding\n" + std::string(PARA_B));
}

TEST(NewsArticleExtractor, CommentsAndSelfClosingSkipsAreHandled) {
  const std::string html = std::string("<!-- <p>commented out entirely</p> --><figure/><p>") + PARA_A +
                           "</p><figure><p>Photo credit</p></figure>";
  EXPECT_EQ(extract(html), PARA_A);
}

TEST(NewsArticleExtractor, TruncatesOnTheCapWithoutSplittingUtf8) {
  const std::string para = "<p>\xE2\x80\x94 a long dash paragraph that runs past the buffer end and keeps going</p>";
  const std::string text = extract(para, 32);
  EXPECT_LE(text.size(), 31u);
  // The sink appends U+2026 when it truncates, and never cuts a sequence.
  EXPECT_EQ(text.substr(text.size() - 3), "\xE2\x80\xA6");
}

TEST(NewsArticleExtractor, ConsentWallYieldsAlmostNothing) {
  // What a bot check looks like: the caller compares against MIN_ARTICLE_BYTES.
  const std::string html = "<html><body><p>Enable JavaScript and cookies to continue</p></body></html>";
  EXPECT_LT(extract(html).size(), NewsArticleExtractor::MIN_ARTICLE_BYTES);
}

}  // namespace
