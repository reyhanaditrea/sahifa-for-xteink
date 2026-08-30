#pragma once

#include <cstddef>
#include <cstdint>

// International outlets that ship with the firmware. They can be switched off
// but never removed, so the table lives in flash and the store persists only a
// disabled bitmask -- an outlet added by a later firmware is then on by default
// for everyone who upgrades.
//
// Each URL was verified to serve a parseable feed with same-day items. DW's is
// RSS 1.0/RDF (items at the document root, dates in dc:date), which is why
// NewsFeedParser matches <item> at any depth rather than only under <channel>.
namespace NewsBuiltins {

struct Feed {
  const char* name;
  const char* url;
};

inline constexpr Feed FEEDS[] = {
    {"BBC World", "https://feeds.bbci.co.uk/news/world/rss.xml"},
    {"Al Jazeera", "https://www.aljazeera.com/xml/rss/all.xml"},
    {"France 24", "https://www.france24.com/en/rss"},
    {"DW World", "https://rss.dw.com/rdf/rss-en-world"},
    {"NPR World", "https://feeds.npr.org/1004/rss.xml"},
    // Sky and CBC both publish three days of items, which is what makes the
    // oldest tab useful on a first sync -- BBC, Al Jazeera and NPR only carry
    // one or two. (Arab News was dropped: its feed sat three weeks stale, so
    // nothing it published could ever land inside the window.)
    {"Sky News World", "https://feeds.skynews.com/feeds/rss/world.xml"},
    {"CBC World", "https://www.cbc.ca/webfeed/rss/rss-world"},
};

inline constexpr size_t COUNT = sizeof(FEEDS) / sizeof(FEEDS[0]);
static_assert(COUNT <= 16, "the disabled bitmask in NewsFeedStore is 16 bits");

}  // namespace NewsBuiltins
