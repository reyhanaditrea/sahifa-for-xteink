#include "prayer/PrayerHijri.h"

#include "prayer/PrayerHijriTable.h"

namespace PrayerHijri {

namespace {
// Days in month `month` (1-12) of the year at `index` in the table.
int monthLength(const uint16_t index, const int month) {
  return (PrayerHijriData::MONTH_LENGTHS[index] & (1u << (month - 1))) ? 30 : 29;
}

int yearLength(const uint16_t index) {
  int days = 0;
  for (int month = 1; month <= 12; ++month) days += monthLength(index, month);
  return days;
}
}  // namespace

bool fromDayOrdinal(const int32_t dayOrdinal, HijriDate& out) {
  int32_t remaining = dayOrdinal - PrayerHijriData::FIRST_DAY_ORDINAL;
  if (remaining < 0) return false;

  // A hundred iterations at worst, once per screen entry: a binary search over
  // cumulative year lengths would need a second table to save nothing.
  for (uint16_t index = 0; index < PrayerHijriData::YEAR_COUNT; ++index) {
    const int32_t length = yearLength(index);
    if (remaining >= length) {
      remaining -= length;
      continue;
    }
    for (int month = 1; month <= 12; ++month) {
      const int32_t days = monthLength(index, month);
      if (remaining < days) {
        out.year = static_cast<uint16_t>(PrayerHijriData::FIRST_YEAR + index);
        out.month = static_cast<uint8_t>(month);
        out.day = static_cast<uint8_t>(remaining + 1);
        return true;
      }
      remaining -= days;
    }
    return false;  // unreachable: the month loop covers the whole year
  }
  return false;  // past the end of the table
}

}  // namespace PrayerHijri
