#include <gtest/gtest.h>

#include <cstring>

#include "prayer/PrayerPure.h"

namespace {

TEST(PrayerPureTimes, MinutesSinceMidnight) {
  EXPECT_EQ(PrayerPure::timeToMinutes("00:00"), 0);
  EXPECT_EQ(PrayerPure::timeToMinutes("15:14"), 15 * 60 + 14);
  EXPECT_EQ(PrayerPure::timeToMinutes("24:00"), -1);
  EXPECT_EQ(PrayerPure::timeToMinutes("12:60"), -1);
  EXPECT_EQ(PrayerPure::timeToMinutes("4:31"), -1);
  EXPECT_EQ(PrayerPure::timeToMinutes(nullptr), -1);
}

TEST(PrayerPureLocalTime, AppliesTheTimezoneOffset) {
  // The bug this covers: the RTC reports UTC, while the timings are local
  // wall-clock. Jakarta (UTC+7) at 23:35 local is 16:35 UTC; comparing the
  // raw UTC minutes against a 17:54 Maghrib made it look 79 minutes away.
  constexpr int32_t WIB = 7 * 3600;
  EXPECT_EQ(PrayerPure::wrapMinutes(16 * 60 + 35 + WIB / 60), 23 * 60 + 35);
  EXPECT_EQ(PrayerPure::wrapMinutes(-30), 1410);
  EXPECT_EQ(PrayerPure::wrapMinutes(1440), 0);

  // 2026-08-20T16:35:00Z is 23:35 in Jakarta.
  EXPECT_EQ(PrayerPure::localMinutesOfDay(1787243700LL, WIB), 23 * 60 + 35);
  // Crossing midnight westward stays on the clock face.
  EXPECT_EQ(PrayerPure::localMinutesOfDay(1787243700LL, -8 * 3600), 8 * 60 + 35);
}

TEST(PrayerPureNext, SkipsSunriseAndReturnsTheNextPrayer) {
  const int times[PrayerPure::TIMING_COUNT] = {4 * 60 + 38,  5 * 60 + 52,  11 * 60 + 54,
                                               15 * 60 + 14, 17 * 60 + 54, 19 * 60 + 3};
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 4 * 60), 0);        // before Fajr
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 5 * 60), 2);        // after Fajr: Dhuhr, not Sunrise
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 14 * 60 + 32), 3);  // Asr
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 19 * 60 + 3), -1);  // Isha has started: nothing left today
  EXPECT_EQ(PrayerPure::nextPrayerIndex(times, 23 * 60 + 35), -1);
}

TEST(PrayerPureLocalTime, FormatsAClockFace) {
  char buf[12];
  PrayerPure::formatClock(buf, sizeof(buf), 14 * 60 + 2, false);
  EXPECT_STREQ(buf, "14:02");
  PrayerPure::formatClock(buf, sizeof(buf), 14 * 60 + 2, true);
  EXPECT_STREQ(buf, "2:02 PM");
  PrayerPure::formatClock(buf, sizeof(buf), 0, true);
  EXPECT_STREQ(buf, "12:00 AM");
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
