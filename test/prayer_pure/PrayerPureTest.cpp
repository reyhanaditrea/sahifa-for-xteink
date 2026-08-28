#include <gtest/gtest.h>

#include <cstring>

#include "prayer/PrayerPure.h"

namespace {

TEST(PrayerPureUrl, FormatsFixedPointCoordinates) {
  char url[192];
  // Jakarta: negative latitude, positive longitude, Kemenag method.
  PrayerPure::buildCalendarUrl(url, sizeof(url), 2026, 8, -621, 10685, 20);
  EXPECT_STREQ(url, "https://api.aladhan.com/v1/calendar/2026/8?latitude=-6.21&longitude=106.85&method=20&school=0");
}

TEST(PrayerPureUrl, KeepsLeadingZeroInTheFraction) {
  char url[192];
  PrayerPure::buildCalendarUrl(url, sizeof(url), 2026, 12, 5107, -5, 3);
  EXPECT_NE(strstr(url, "latitude=51.07"), nullptr) << url;
  // -0.05 must keep its sign even though the whole-degree part is zero.
  EXPECT_NE(strstr(url, "longitude=-0.05"), nullptr) << url;
}

TEST(PrayerPureTimes, CopiesOnlyTheHhMmPrefix) {
  char out[6];
  EXPECT_TRUE(PrayerPure::copyTime("04:31 (WIB)", out, sizeof(out)));
  EXPECT_STREQ(out, "04:31");
  EXPECT_TRUE(PrayerPure::copyTime("19:05", out, sizeof(out)));
  EXPECT_STREQ(out, "19:05");
  EXPECT_FALSE(PrayerPure::copyTime("4:31", out, sizeof(out)));
  EXPECT_FALSE(PrayerPure::copyTime("", out, sizeof(out)));
}

TEST(PrayerPureTimes, MinutesSinceMidnight) {
  EXPECT_EQ(PrayerPure::timeToMinutes("00:00"), 0);
  EXPECT_EQ(PrayerPure::timeToMinutes("15:14"), 15 * 60 + 14);
  EXPECT_EQ(PrayerPure::timeToMinutes("24:00"), -1);
  EXPECT_EQ(PrayerPure::timeToMinutes("12:60"), -1);
  EXPECT_EQ(PrayerPure::timeToMinutes(nullptr), -1);
}

TEST(PrayerPureNext, SkipsSunriseAndReturnsTheNextPrayer) {
  const char times[PrayerPure::TIMING_COUNT][6] = {"04:38", "05:52", "11:54", "15:14", "17:54", "19:03"};
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 4 * 60), 0);        // before Fajr
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 5 * 60), 2);        // after Fajr: Dhuhr, not Sunrise
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 14 * 60 + 32), 3);  // Asr
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 19 * 60 + 3), -1);  // Isha has started: nothing left today
}

TEST(PrayerPureMonthFile, ParsesOnlyWellFormedNames) {
  int year = 0;
  unsigned month = 0;
  EXPECT_TRUE(PrayerPure::parseMonthFileName("2026-09.json", year, month));
  EXPECT_EQ(year, 2026);
  EXPECT_EQ(month, 9u);
  EXPECT_FALSE(PrayerPure::parseMonthFileName("2026-13.json", year, month));
  EXPECT_FALSE(PrayerPure::parseMonthFileName("2026-00.json", year, month));
  EXPECT_FALSE(PrayerPure::parseMonthFileName("dl.tmp", year, month));
  EXPECT_FALSE(PrayerPure::parseMonthFileName("2026-9.json", year, month));
  EXPECT_FALSE(PrayerPure::parseMonthFileName(nullptr, year, month));
}

TEST(PrayerPureCoordinate, UsesHemisphereLetters) {
  char buf[16];
  PrayerPure::formatCoordinate(buf, sizeof(buf), -621, true);
  EXPECT_STREQ(buf, "S 6.21");
  PrayerPure::formatCoordinate(buf, sizeof(buf), 10685, false);
  EXPECT_STREQ(buf, "E 106.85");
  PrayerPure::formatCoordinate(buf, sizeof(buf), 0, true);
  EXPECT_STREQ(buf, "N 0.00");
}

}  // namespace
