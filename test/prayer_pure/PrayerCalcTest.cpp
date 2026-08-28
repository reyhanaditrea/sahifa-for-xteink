#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "prayer/PrayerCalc.h"
#include "prayer/PrayerHijri.h"
#include "prayer/PrayerPure.h"

namespace {

// Kemenag's angles, without the ihtiyati margin its published schedule adds.
PrayerCalc::Method kemenagAngles() {
  PrayerCalc::Method m{};
  m.fajrAngle = 20.0;
  m.ishaAngle = 18.0;
  m.ishaMinutes = 0;
  m.asrShadow = 1;
  m.roundUp = false;
  for (auto& offset : m.offsets) offset = 0;
  return m;
}

// The same, as the Kemenag jadwal prints it: rounded up, then the margin.
PrayerCalc::Method kemenagSchedule() {
  PrayerCalc::Method m = kemenagAngles();
  m.roundUp = true;
  const int8_t ihtiyati[] = {2, -4, 3, 2, 3, 2};
  for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) m.offsets[i] = ihtiyati[i];
  return m;
}

std::string hhmm(const int minutes) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return buf;
}

// Jakarta, in hundredths of a degree, at UTC+7.
constexpr int32_t JKT_LAT = -621;
constexpr int32_t JKT_LNG = 10685;
constexpr int32_t WIB = 7 * 3600;

TEST(PrayerCalcTest, MatchesTheReferenceCalculationForJakarta) {
  // Cross-checked against the Aladhan API for 2026-08-01 with method 20
  // (Kementerian Agama Republik Indonesia): the whole month agreed to the
  // minute, which is the point -- an API adds no accuracy over doing this
  // here, it just runs the same arithmetic on someone else's computer.
  PrayerCalc::DayTimes t{};
  ASSERT_TRUE(PrayerCalc::compute(2026, 8, 1, JKT_LAT, JKT_LNG, WIB, kemenagAngles(), t));
  EXPECT_TRUE(t.exact);
  EXPECT_EQ(hhmm(t.minutes[0]), "04:43");  // Fajr
  EXPECT_EQ(hhmm(t.minutes[1]), "06:04");  // Sunrise
  EXPECT_EQ(hhmm(t.minutes[2]), "11:59");  // Dhuhr
  EXPECT_EQ(hhmm(t.minutes[3]), "15:21");  // Asr
  EXPECT_EQ(hhmm(t.minutes[4]), "17:54");  // Maghrib
  EXPECT_EQ(hhmm(t.minutes[5]), "19:07");  // Isha
}

TEST(PrayerCalcTest, StaysWithinAMinuteOfKemenagsPublishedSchedule) {
  // Kemenag's own jadwal for Jakarta (bimasislam, via api.myquran.com). The
  // round-up and the ihtiyati margin are what put the device on the same
  // minute as the schedule on the mosque wall; the residual is Kemenag's
  // solar model differing from this one by a few seconds, which lands on
  // either side of a minute boundary. Across the whole of August 2026 and
  // five Indonesian cities no timing was ever more than a minute out, so
  // that is what this asserts -- claiming exact equality would be claiming
  // to have reimplemented their ephemeris.
  struct Day {
    unsigned day;
    const char* published[PrayerPure::TIMING_COUNT];
  };
  constexpr Day DAYS[] = {
      {1, {"04:45", "06:00", "12:03", "15:24", "17:58", "19:09"}},
      {15, {"04:43", "05:57", "12:01", "15:21", "17:58", "19:08"}},
      {28, {"04:39", "05:52", "11:58", "15:16", "17:56", "19:06"}},
  };

  for (const Day& day : DAYS) {
    PrayerCalc::DayTimes t{};
    ASSERT_TRUE(PrayerCalc::compute(2026, 8, day.day, JKT_LAT, JKT_LNG, WIB, kemenagSchedule(), t)) << day.day;
    for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) {
      const int published = PrayerPure::timeToMinutes(day.published[i]);
      EXPECT_LE(std::abs(t.minutes[i] - published), 1) << "day " << day.day << " timing " << i << ": got "
                                                       << hhmm(t.minutes[i]) << ", jadwal says " << day.published[i];
    }
  }
}

TEST(PrayerCalcTest, TimingsRunInOrderThroughTheYear) {
  const PrayerCalc::Method method = kemenagSchedule();
  for (unsigned month = 1; month <= 12; ++month) {
    PrayerCalc::DayTimes t{};
    ASSERT_TRUE(PrayerCalc::compute(2026, month, 15, JKT_LAT, JKT_LNG, WIB, method, t)) << month;
    for (int i = 1; i < PrayerPure::TIMING_COUNT; ++i) {
      EXPECT_LT(t.minutes[i - 1], t.minutes[i]) << "month " << month << " timing " << i;
    }
  }
}

TEST(PrayerCalcTest, TheOffsetIsTheOnlyThingThatMovesWithTheTimezone) {
  PrayerCalc::DayTimes jakarta{}, shifted{};
  const PrayerCalc::Method method = kemenagAngles();
  ASSERT_TRUE(PrayerCalc::compute(2026, 8, 28, JKT_LAT, JKT_LNG, WIB, method, jakarta));
  // Same place, device set an hour ahead: every timing shifts by exactly that.
  ASSERT_TRUE(PrayerCalc::compute(2026, 8, 28, JKT_LAT, JKT_LNG, WIB + 3600, method, shifted));
  for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) {
    EXPECT_EQ(shifted.minutes[i] - jakarta.minutes[i], 60) << i;
  }
}

TEST(PrayerCalcTest, HighLatitudeSummerFallsBackAndSaysSo) {
  // Tromso in June: the sun never sinks 18 degrees below the horizon, so Fajr
  // and Isha have no true solution and the night-fraction rule stands in.
  PrayerCalc::Method mwl = kemenagAngles();
  mwl.fajrAngle = 18.0;
  mwl.ishaAngle = 17.0;
  PrayerCalc::DayTimes t{};
  ASSERT_TRUE(PrayerCalc::compute(2026, 6, 21, 6965, 1899, 2 * 3600, mwl, t));
  EXPECT_FALSE(t.exact);
  for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) {
    EXPECT_GE(t.minutes[i], 0);
    EXPECT_LT(t.minutes[i], 1440);
  }

  // London in August still resolves normally.
  PrayerCalc::DayTimes london{};
  ASSERT_TRUE(PrayerCalc::compute(2026, 8, 28, 5151, -13, 3600, mwl, london));
  EXPECT_TRUE(london.exact);
}

TEST(PrayerCalcTest, RejectsAnImpossibleDate) {
  PrayerCalc::DayTimes t{};
  EXPECT_FALSE(PrayerCalc::compute(2026, 13, 1, JKT_LAT, JKT_LNG, WIB, kemenagAngles(), t));
  EXPECT_FALSE(PrayerCalc::compute(2026, 8, 0, JKT_LAT, JKT_LNG, WIB, kemenagAngles(), t));
}

TEST(PrayerHijriTest, ConvertsAgainstTheUmmAlQuraTable) {
  PrayerHijri::HijriDate h{};
  // 2026-08-28 is 15 Rabi al-awwal 1448 (day ordinal 20693).
  ASSERT_TRUE(PrayerHijri::fromDayOrdinal(20693, h));
  EXPECT_EQ(h.year, 1448);
  EXPECT_EQ(h.month, 3);
  EXPECT_EQ(h.day, 15);

  // The table's own first day: 1 Muharram 1400 AH = 1979-11-20.
  ASSERT_TRUE(PrayerHijri::fromDayOrdinal(3610, h));
  EXPECT_EQ(h.year, 1400);
  EXPECT_EQ(h.month, 1);
  EXPECT_EQ(h.day, 1);
}

TEST(PrayerHijriTest, RefusesDatesOutsideTheTable) {
  PrayerHijri::HijriDate h{};
  EXPECT_FALSE(PrayerHijri::fromDayOrdinal(0, h));      // 1970, before the table starts
  EXPECT_FALSE(PrayerHijri::fromDayOrdinal(80000, h));  // 2189, long past its end
}

TEST(PrayerHijriTest, EveryDayInTheTableAdvancesByExactlyOne) {
  PrayerHijri::HijriDate previous{};
  ASSERT_TRUE(PrayerHijri::fromDayOrdinal(3610, previous));
  for (int32_t ordinal = 3611; ordinal < 3610 + 400; ++ordinal) {
    PrayerHijri::HijriDate current{};
    ASSERT_TRUE(PrayerHijri::fromDayOrdinal(ordinal, current)) << ordinal;
    const bool sameMonth = current.year == previous.year && current.month == previous.month;
    if (sameMonth) {
      EXPECT_EQ(current.day, previous.day + 1) << ordinal;
    } else {
      EXPECT_EQ(current.day, 1) << ordinal;
      EXPECT_TRUE(previous.day == 29 || previous.day == 30) << ordinal;
    }
    previous = current;
  }
}

}  // namespace
