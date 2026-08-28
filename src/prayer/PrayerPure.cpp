#include "prayer/PrayerPure.h"

#include <cstdio>
#include <cstring>

namespace PrayerPure {

namespace {
bool isDigit(const char c) { return c >= '0' && c <= '9'; }
}  // namespace

int wrapMinutes(const int minutes) { return ((minutes % 1440) + 1440) % 1440; }

int localMinutesOfDay(const int64_t epochSeconds, const int32_t offsetSeconds) {
  const int64_t local = epochSeconds + offsetSeconds;
  // C++ truncates toward zero, so a pre-1970 epoch would give a negative
  // remainder; wrapMinutes puts it back on the clock face either way.
  return wrapMinutes(static_cast<int>((local % 86400) / 60));
}

int timeToMinutes(const char* hhmm) {
  if (hhmm == nullptr) return -1;
  if (!(isDigit(hhmm[0]) && isDigit(hhmm[1]) && hhmm[2] == ':' && isDigit(hhmm[3]) && isDigit(hhmm[4]))) return -1;
  const int h = (hhmm[0] - '0') * 10 + (hhmm[1] - '0');
  const int m = (hhmm[3] - '0') * 10 + (hhmm[4] - '0');
  if (h > 23 || m > 59) return -1;
  return h * 60 + m;
}

void formatClock(char* buf, const size_t len, const int minutesOfDay, const bool use12Hour) {
  const int wrapped = wrapMinutes(minutesOfDay);
  const int hour24 = wrapped / 60;
  const int minute = wrapped % 60;
  if (use12Hour) {
    const bool pm = hour24 >= 12;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, len, "%d:%02d %s", hour12, minute, pm ? "PM" : "AM");
  } else {
    snprintf(buf, len, "%02d:%02d", hour24, minute);
  }
}

int nextPrayerIndex(const int minutes[TIMING_COUNT], const int nowMinutes) {
  for (int i = 0; i < TIMING_COUNT; ++i) {
    if (i == 1) continue;  // sunrise ends Fajr, it is not itself a prayer
    if (minutes[i] > nowMinutes) return i;
  }
  return -1;
}

void formatCoordinate(char* buf, const size_t len, const int32_t valueE2, const bool isLatitude) {
  const bool negative = valueE2 < 0;
  const int32_t abs = negative ? -valueE2 : valueE2;
  const char hemisphere = isLatitude ? (negative ? 'S' : 'N') : (negative ? 'W' : 'E');
  snprintf(buf, len, "%c %ld.%02ld", hemisphere, static_cast<long>(abs / 100), static_cast<long>(abs % 100));
}

}  // namespace PrayerPure
