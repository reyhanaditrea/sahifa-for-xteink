#pragma once

#include <cstdint>

// Gregorian -> Umm al-Qura Hijri, over the span the generated table covers
// (src/prayer/PrayerHijriTable.h, 1400-1499 AH = 1979-2077 CE). Outside that
// span there is no honest answer to give, so the caller gets false and the
// screen leaves the Hijri line blank rather than printing a drifted date.
namespace PrayerHijri {

struct HijriDate {
  uint16_t year;
  uint8_t month;  // 1-12
  uint8_t day;    // 1-30
};

// dayOrdinal is days since 1970-01-01, as TimeUtils counts them.
bool fromDayOrdinal(int32_t dayOrdinal, HijriDate& out);

}  // namespace PrayerHijri
