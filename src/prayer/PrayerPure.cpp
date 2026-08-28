#include "prayer/PrayerPure.h"

#include <cstdio>
#include <cstring>

namespace PrayerPure {

const char* const TIMING_KEYS[TIMING_COUNT] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};

namespace {
bool isDigit(const char c) { return c >= '0' && c <= '9'; }
}  // namespace

void buildCalendarUrl(char* buf, const size_t len, const int year, const unsigned month, const int32_t latE2,
                      const int32_t lngE2, const uint8_t method) {
  const char* latSign = latE2 < 0 ? "-" : "";
  const char* lngSign = lngE2 < 0 ? "-" : "";
  const int32_t latAbs = latE2 < 0 ? -latE2 : latE2;
  const int32_t lngAbs = lngE2 < 0 ? -lngE2 : lngE2;
  snprintf(buf, len,
           "https://api.aladhan.com/v1/calendar/%d/%u?latitude=%s%ld.%02ld&longitude=%s%ld.%02ld&method=%u&school=0",
           year, month, latSign, static_cast<long>(latAbs / 100), static_cast<long>(latAbs % 100), lngSign,
           static_cast<long>(lngAbs / 100), static_cast<long>(lngAbs % 100), static_cast<unsigned>(method));
}

bool copyTime(const char* in, char* out, const size_t outLen) {
  if (in == nullptr || outLen < 6) return false;
  if (!(isDigit(in[0]) && isDigit(in[1]) && in[2] == ':' && isDigit(in[3]) && isDigit(in[4]))) return false;
  memcpy(out, in, 5);
  out[5] = '\0';
  return true;
}

int timeToMinutes(const char* hhmm) {
  if (hhmm == nullptr) return -1;
  if (!(isDigit(hhmm[0]) && isDigit(hhmm[1]) && hhmm[2] == ':' && isDigit(hhmm[3]) && isDigit(hhmm[4]))) return -1;
  const int h = (hhmm[0] - '0') * 10 + (hhmm[1] - '0');
  const int m = (hhmm[3] - '0') * 10 + (hhmm[4] - '0');
  if (h > 23 || m > 59) return -1;
  return h * 60 + m;
}

int nextPrayerIndex(const char times[TIMING_COUNT][6], const int nowMinutes) {
  for (int i = 0; i < TIMING_COUNT; ++i) {
    if (i == 1) continue;  // Sunrise marks the end of Fajr, it is not itself a prayer
    const int t = timeToMinutes(times[i]);
    if (t >= 0 && t > nowMinutes) return i;
  }
  return -1;
}

bool parseMonthFileName(const char* name, int& year, unsigned& month) {
  // YYYY-MM.json
  if (name == nullptr || strlen(name) != 12) return false;
  for (int i = 0; i < 4; ++i) {
    if (!isDigit(name[i])) return false;
  }
  if (name[4] != '-' || !isDigit(name[5]) || !isDigit(name[6]) || strcmp(name + 7, ".json") != 0) return false;
  year = (name[0] - '0') * 1000 + (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
  month = static_cast<unsigned>((name[5] - '0') * 10 + (name[6] - '0'));
  return month >= 1 && month <= 12;
}

void formatCoordinate(char* buf, const size_t len, const int32_t valueE2, const bool isLatitude) {
  const bool negative = valueE2 < 0;
  const int32_t abs = negative ? -valueE2 : valueE2;
  const char hemisphere = isLatitude ? (negative ? 'S' : 'N') : (negative ? 'W' : 'E');
  snprintf(buf, len, "%c %ld.%02ld", hemisphere, static_cast<long>(abs / 100), static_cast<long>(abs % 100));
}

}  // namespace PrayerPure
