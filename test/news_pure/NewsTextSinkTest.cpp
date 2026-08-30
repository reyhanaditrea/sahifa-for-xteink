#include <gtest/gtest.h>

#include <string>

#include "news/NewsTextSink.h"

namespace {

std::string render(const std::string& html, const size_t capacity = 512) {
  std::string buffer(capacity, '\0');
  NewsTextSink sink(buffer.data(), capacity);
  sink.addText(html.data(), html.size());
  const size_t len = sink.finish();
  return std::string(buffer.data(), len);
}

// Feeds arrive in arbitrary chunks, so every rule must survive being split.
std::string renderChunked(const std::string& html, const size_t chunk) {
  std::string buffer(512, '\0');
  NewsTextSink sink(buffer.data(), buffer.size());
  for (size_t i = 0; i < html.size(); i += chunk) {
    sink.addText(html.data() + i, std::min(chunk, html.size() - i));
  }
  const size_t len = sink.finish();
  return std::string(buffer.data(), len);
}

TEST(NewsTextSink, StripsTagsAndCollapsesWhitespace) {
  EXPECT_EQ(render("<p>Hello   \n  world</p>"), "Hello world");
  EXPECT_EQ(render("  <b>Bold</b> text  "), "Bold text");
}

TEST(NewsTextSink, BlockTagsBecomeParagraphs) {
  EXPECT_EQ(render("<p>One</p><p>Two</p>"), "One\nTwo");
  EXPECT_EQ(render("One<br>Two"), "One\nTwo");
  EXPECT_EQ(render("<ul><li>A</li><li>B</li></ul>"), "A\nB");
  // Runs of block markup collapse to a single break, not a stack of them.
  EXPECT_EQ(render("<div><p>One</p></div>\n\n<div><p>Two</p></div>"), "One\nTwo");
  // No leading break when the body opens with a block tag.
  EXPECT_EQ(render("<p>Only</p>"), "Only");
}

TEST(NewsTextSink, DecodesEntities) {
  EXPECT_EQ(render("Fish &amp; chips"), "Fish & chips");
  EXPECT_EQ(render("&lt;tag&gt;"), "<tag>");
  EXPECT_EQ(render("caf&#233;"), "caf\xC3\xA9");
  EXPECT_EQ(render("caf&#xE9;"), "caf\xC3\xA9");
  EXPECT_EQ(render("&quot;quoted&quot;"), "\"quoted\"");
  // nbsp reads as a plain space; keeping U+00A0 would defeat the line breaker.
  EXPECT_EQ(render("a&nbsp;b"), "a b");
  EXPECT_EQ(render("a&#160;b"), "a b");
}

TEST(NewsTextSink, KeepsMalformedAmpersandsVerbatim) {
  EXPECT_EQ(render("Tom & Jerry"), "Tom & Jerry");
  EXPECT_EQ(render("&notarealentity;"), "&notarealentity;");
}

TEST(NewsTextSink, DropsScriptStyleAndComments) {
  EXPECT_EQ(render("A<script>var x = 1;</script>B"), "AB");
  EXPECT_EQ(render("A<style>.c{color:red}</style>B"), "AB");
  EXPECT_EQ(render("A<!-- hidden -->B"), "AB");
  EXPECT_EQ(render("A<!-- multi -- dash -->B"), "AB");
}

TEST(NewsTextSink, HandlesAttributesAndSelfClosingTags) {
  EXPECT_EQ(render("<a href=\"http://x/y?a=1&amp;b=2\">link</a>"), "link");
  EXPECT_EQ(render("One<br />Two"), "One\nTwo");
  EXPECT_EQ(render("<img src=\"x.png\" />Caption"), "Caption");
}

TEST(NewsTextSink, SurvivesChunkBoundaries) {
  const std::string html = "<p>Fish &amp; chips</p><p>Second &#233;clair</p>";
  const std::string expected =
      "Fish & chips\nSecond \xC3\xA9"
      "clair";
  for (size_t chunk = 1; chunk <= 8; chunk++) {
    EXPECT_EQ(renderChunked(html, chunk), expected) << "chunk size " << chunk;
  }
}

TEST(NewsTextSink, TruncatesWithEllipsis) {
  std::string buffer(24, '\0');
  NewsTextSink sink(buffer.data(), buffer.size());
  const std::string longText(100, 'x');
  sink.addText(longText.data(), longText.size());
  const size_t len = sink.finish();
  EXPECT_TRUE(sink.truncated());
  EXPECT_LT(len, buffer.size());
  const std::string out(buffer.data(), len);
  EXPECT_EQ(out.substr(out.size() - 3), "\xE2\x80\xA6");
}

TEST(NewsTextSink, TruncationNeverSplitsUtf8) {
  // Every cut point of a run of 2-byte characters must land on a boundary.
  for (size_t capacity = 8; capacity <= 40; capacity++) {
    std::string buffer(capacity, '\0');
    NewsTextSink sink(buffer.data(), capacity);
    std::string text;
    for (int i = 0; i < 40; i++) text += "\xC3\xA9";
    sink.addText(text.data(), text.size());
    const size_t len = sink.finish();
    const std::string out(buffer.data(), len);
    // A valid UTF-8 string never ends on a lead byte with its continuation cut.
    size_t i = 0;
    while (i < out.size()) {
      const unsigned char c = static_cast<unsigned char>(out[i]);
      const size_t seq = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
      ASSERT_LE(i + seq, out.size()) << "split sequence at capacity " << capacity;
      i += seq;
    }
  }
}

TEST(NewsTextSink, XhtmlElementsBreakParagraphs) {
  std::string buffer(128, '\0');
  NewsTextSink sink(buffer.data(), buffer.size());
  // Atom type="xhtml" content arrives as real elements, not as text.
  sink.openElement("p");
  sink.addText("One", 3);
  sink.closeElement("p");
  sink.openElement("p");
  sink.addText("Two", 3);
  sink.closeElement("p");
  const size_t len = sink.finish();
  EXPECT_EQ(std::string(buffer.data(), len), "One\nTwo");
}

TEST(NewsTextSink, ResetClearsState) {
  std::string buffer(64, '\0');
  NewsTextSink sink(buffer.data(), buffer.size());
  sink.addText("first", 5);
  sink.reset();
  sink.addText("second", 6);
  const size_t len = sink.finish();
  EXPECT_EQ(std::string(buffer.data(), len), "second");
}

TEST(NewsTextSink, CdataDelimitersWouldSwallowTheBody) {
  // Not a feature -- a guard. Expat reports the CDATA delimiters to the default
  // handler unless CDATA handlers are installed, and if the parser forwarded
  // them here the opening marker would be read as an unterminated tag and eat
  // the article. NewsFeedParser installs no-op CDATA handlers so this text
  // never reaches the sink; the assertion documents what happens if that
  // regresses.
  EXPECT_EQ(render("<![CDATA[Real body text]]>"), "");
  // The content on its own, which is what the sink actually receives.
  EXPECT_EQ(render("Real body text"), "Real body text");
}

TEST(NewsTextSink, RealWorldDescription) {
  // Shape BBC and detik actually ship inside CDATA.
  const std::string html =
      "<p>Temperatures will stay above 40C &amp; rise further.</p>"
      "<p>Officials urged people to stay indoors.</p>"
      "<img src=\"https://example/pic.jpg\" alt=\"heat\">";
  EXPECT_EQ(render(html), "Temperatures will stay above 40C & rise further.\nOfficials urged people to stay indoors.");
}

}  // namespace
