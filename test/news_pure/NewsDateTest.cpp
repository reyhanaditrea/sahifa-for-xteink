#include <gtest/gtest.h>

#include "news/NewsDate.h"

namespace {

// 2026-08-29 00:00:00 UTC
constexpr uint32_t AUG_29_2026_UTC = 1787961600U;
constexpr int32_t WIB = 7 * 3600;  // UTC+7, the device's home offset

uint32_t ordinalUtc(const char* s) { return NewsDate::parseFeedDateToLocalOrdinal(s, 0); }

TEST(NewsDateRfc822, FullFormWithNumericZone) {
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("Sat, 29 Aug 2026 00:00:00 +0000"), AUG_29_2026_UTC);
  // +0300 means the same wall clock happened three hours earlier in UTC.
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("Sat, 29 Aug 2026 03:00:00 +0300"), AUG_29_2026_UTC);
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("Sat, 29 Aug 2026 03:00:00 +03:00"), AUG_29_2026_UTC);
}

TEST(NewsDateRfc822, NamedZonesAndMissingWeekday) {
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("Sat, 29 Aug 2026 00:00:00 GMT"), AUG_29_2026_UTC);
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("29 Aug 2026 00:00:00 GMT"), AUG_29_2026_UTC);
  // EST is UTC-5, so 19:00 on the 28th is midnight UTC on the 29th.
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("Fri, 28 Aug 2026 19:00:00 EST"), AUG_29_2026_UTC);
  // An unknown alphabetic zone is treated as UTC, per RFC 822.
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("Sat, 29 Aug 2026 00:00:00 XYZ"), AUG_29_2026_UTC);
}

TEST(NewsDateRfc822, TwoDigitYearAndNoSeconds) {
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("Sat, 29 Aug 26 00:00 +0000"), AUG_29_2026_UTC);
}

TEST(NewsDateRfc3339, AtomVariants) {
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("2026-08-29T00:00:00Z"), AUG_29_2026_UTC);
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("2026-08-29T03:00:00+03:00"), AUG_29_2026_UTC);
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("2026-08-29T00:00:00.123Z"), AUG_29_2026_UTC);
  // dc:date on some RDF feeds omits the seconds.
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("2026-08-29T00:00Z"), AUG_29_2026_UTC);
  // Date only: midnight UTC.
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("2026-08-29"), AUG_29_2026_UTC);
}

TEST(NewsDateInvalid, UnknownIsZero) {
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch(nullptr), 0U);
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch(""), 0U);
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("no date here"), 0U);
  EXPECT_EQ(NewsDate::parseFeedDateToEpoch("Sat, 29 Xyz 2026 00:00:00 GMT"), 0U);
  // Pre-2020 epochs read as an unsynced clock, not an old article.
  EXPECT_EQ(NewsDate::parseFeedDateToLocalOrdinal("Mon, 01 Jan 1990 00:00:00 GMT", 0), 0U);
}

TEST(NewsDateOrdinal, ConsecutiveDaysDiffer) {
  const uint32_t d29 = ordinalUtc("Sat, 29 Aug 2026 12:00:00 +0000");
  const uint32_t d28 = ordinalUtc("Fri, 28 Aug 2026 12:00:00 +0000");
  EXPECT_EQ(d29 - d28, 1U);
}

TEST(NewsDateOrdinal, LocalOffsetMovesTheDay) {
  // 22:00 UTC on the 28th is already the 29th in UTC+7 -- the case that decides
  // whether a Jakarta reader sees an item under Today or Yesterday.
  const char* evening = "Fri, 28 Aug 2026 22:00:00 +0000";
  EXPECT_EQ(NewsDate::parseFeedDateToLocalOrdinal(evening, WIB),
            NewsDate::parseFeedDateToLocalOrdinal("Sat, 29 Aug 2026 12:00:00 +0700", WIB));
  EXPECT_EQ(NewsDate::parseFeedDateToLocalOrdinal(evening, 0) + 1, NewsDate::parseFeedDateToLocalOrdinal(evening, WIB));
}

TEST(NewsDateOrdinal, FeedZoneIsHonoured) {
  // The same instant written in two zones must land on the same local day.
  EXPECT_EQ(NewsDate::parseFeedDateToLocalOrdinal("Sat, 29 Aug 2026 10:00:00 +1000", WIB),
            NewsDate::parseFeedDateToLocalOrdinal("2026-08-29T00:00:00Z", WIB));
}

}  // namespace
