#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "news/NewsDiscovery.h"

namespace {

using namespace NewsDiscovery;

bool feedHead(const std::string& s) { return looksLikeFeedHead(s.data(), s.size()); }

std::string alternate(const std::string& html, const char* base = "https://blog.arduino.cc/") {
  char out[256] = {};
  if (!findAlternateLink(html.data(), html.size(), base, out, sizeof(out))) return "";
  return out;
}

TEST(NewsDiscoveryHead, RecognisesFeeds) {
  EXPECT_TRUE(feedHead("<?xml version=\"1.0\"?><rss version=\"2.0\">"));
  EXPECT_TRUE(feedHead("<rss version=\"2.0\">"));
  EXPECT_TRUE(feedHead("<feed xmlns=\"http://www.w3.org/2005/Atom\">"));
  EXPECT_TRUE(feedHead("<?xml version=\"1.0\"?>\n<rdf:RDF xmlns=\"...\">"));
  EXPECT_TRUE(feedHead("\xEF\xBB\xBF<?xml version=\"1.0\"?><rss>"));  // BOM
  EXPECT_TRUE(feedHead("   \n\t<RSS VERSION=\"2.0\">"));
}

TEST(NewsDiscoveryHead, RejectsPages) {
  EXPECT_FALSE(feedHead("<!doctype html><html><head>"));
  EXPECT_FALSE(feedHead("<html>"));
  EXPECT_FALSE(feedHead(""));
  // An XHTML page carries an XML declaration but no feed root.
  EXPECT_FALSE(feedHead("<?xml version=\"1.0\"?><!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\">"));
}

TEST(NewsDiscoveryLink, FindsAdvertisedFeed) {
  // The exact line news.detik.com serves.
  EXPECT_EQ(alternate("<link rel=\"alternate\" type=\"application/rss+xml\" "
                      "href=\"https://news.detik.com/googlenews/rss\" />",
                      "https://news.detik.com/"),
            "https://news.detik.com/googlenews/rss");
}

TEST(NewsDiscoveryLink, HandlesAttributeOrderAndQuotes) {
  EXPECT_EQ(alternate("<link type='application/atom+xml' rel='alternate' href='https://x.test/atom.xml'>"),
            "https://x.test/atom.xml");
  EXPECT_EQ(alternate("<LINK REL=\"ALTERNATE\" TYPE=\"APPLICATION/RSS+XML\" HREF=\"https://x.test/f.xml\">"),
            "https://x.test/f.xml");
  // rel omitted entirely, as some CMS templates do.
  EXPECT_EQ(alternate("<link type=\"application/rss+xml\" href=\"https://x.test/f.xml\">"), "https://x.test/f.xml");
}

TEST(NewsDiscoveryLink, ResolvesRelativeHrefs) {
  EXPECT_EQ(alternate("<link rel=\"alternate\" type=\"application/rss+xml\" href=\"/feed/\">"),
            "https://blog.arduino.cc/feed/");
  EXPECT_EQ(alternate("<link rel=\"alternate\" type=\"application/rss+xml\" href=\"feed/\">"),
            "https://blog.arduino.cc/feed/");
  // The base's own path must not leak into the resolved URL.
  EXPECT_EQ(alternate("<link rel=\"alternate\" type=\"application/rss+xml\" href=\"/feed/\">",
                      "https://blog.arduino.cc/some/article?x=1"),
            "https://blog.arduino.cc/feed/");
}

TEST(NewsDiscoveryLink, SkipsNonFeedLinks) {
  EXPECT_EQ(alternate("<link rel=\"stylesheet\" type=\"text/css\" href=\"/a.css\">"), "");
  EXPECT_EQ(alternate("<link rel=\"icon\" href=\"/favicon.ico\">"), "");
  EXPECT_EQ(alternate("<link rel=\"canonical\" href=\"https://x.test/\">"), "");
  // rel="self" belongs to an embedded feed, not to the page's alternate.
  EXPECT_EQ(alternate("<link rel=\"self\" type=\"application/rss+xml\" href=\"/self.xml\">"), "");
  // A stylesheet ahead of the feed link must not stop the scan.
  EXPECT_EQ(alternate("<link rel=\"stylesheet\" href=\"/a.css\">"
                      "<link rel=\"alternate\" type=\"application/rss+xml\" href=\"/feed/\">"),
            "https://blog.arduino.cc/feed/");
  // data-href is not href.
  EXPECT_EQ(alternate("<link rel=\"alternate\" type=\"application/rss+xml\" data-href=\"/feed/\">"), "");
}

TEST(NewsDiscoveryUrl, NormalizesInput) {
  char out[128] = {};
  ASSERT_TRUE(normalizeInputUrl("news.detik.com", out, sizeof(out)));
  EXPECT_STREQ(out, "https://news.detik.com");
  ASSERT_TRUE(normalizeInputUrl("  https://x.test/feed  ", out, sizeof(out)));
  EXPECT_STREQ(out, "https://x.test/feed");
  ASSERT_TRUE(normalizeInputUrl("http://x.test", out, sizeof(out)));
  EXPECT_STREQ(out, "http://x.test");
  EXPECT_FALSE(normalizeInputUrl("", out, sizeof(out)));
  char tiny[8] = {};
  EXPECT_FALSE(normalizeInputUrl("news.detik.com", tiny, sizeof(tiny)));
}

TEST(NewsDiscoveryUrl, TogglesWww) {
  char out[128] = {};
  // A site that serves the feed from only one of the two spellings.
  ASSERT_TRUE(urlWithWwwToggled("https://detik.com", out, sizeof(out)));
  EXPECT_STREQ(out, "https://www.detik.com");
  ASSERT_TRUE(urlWithWwwToggled("https://www.detik.com", out, sizeof(out)));
  EXPECT_STREQ(out, "https://detik.com");
  // The path and query survive the swap.
  ASSERT_TRUE(urlWithWwwToggled("https://example.test/a?b=1", out, sizeof(out)));
  EXPECT_STREQ(out, "https://www.example.test/a?b=1");
  // Scheme is preserved, not normalised.
  ASSERT_TRUE(urlWithWwwToggled("http://example.test", out, sizeof(out)));
  EXPECT_STREQ(out, "http://www.example.test");
  char tiny[10] = {};
  EXPECT_FALSE(urlWithWwwToggled("https://example.test", tiny, sizeof(tiny)));
}

TEST(NewsDiscoveryUrl, OriginAndHost) {
  char out[128] = {};
  ASSERT_TRUE(urlOrigin("https://news.detik.com/googlenews/rss", out, sizeof(out)));
  EXPECT_STREQ(out, "https://news.detik.com");
  ASSERT_TRUE(urlOrigin("https://x.test:8080/a/b?c=d", out, sizeof(out)));
  EXPECT_STREQ(out, "https://x.test:8080");

  ASSERT_TRUE(urlHost("https://news.detik.com/googlenews/rss", out, sizeof(out)));
  EXPECT_STREQ(out, "news.detik.com");
  ASSERT_TRUE(urlHost("https://www.aljazeera.com/xml/rss/all.xml", out, sizeof(out)));
  EXPECT_STREQ(out, "aljazeera.com");
  ASSERT_TRUE(urlHost("https://x.test:8080/a", out, sizeof(out)));
  EXPECT_STREQ(out, "x.test");
}

}  // namespace
