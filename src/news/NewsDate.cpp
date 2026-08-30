#include "news/NewsDate.h"

#include <cstring>

namespace {

// Earliest epoch we treat as a real clock reading (2020-01-01). Anything below
// is an unset RTC, not a genuinely old article.
constexpr uint32_t MIN_VALID_EPOCH = 1577836800U;

constexpr char MONTHS[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

bool isDigit(const char c) { return c >= '0' && c <= '9'; }

const char* skipSpaces(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// Reads up to maxDigits digits; returns nullptr when fewer than minDigits are
// present so callers can bail without a separate length check.
const char* readNumber(const char* p, const int minDigits, const int maxDigits, int& out) {
  int value = 0;
  int count = 0;
  while (count < maxDigits && isDigit(*p)) {
    value = value * 10 + (*p - '0');
    p++;
    count++;
  }
  if (count < minDigits) return nullptr;
  out = value;
  return p;
}

int monthFromName(const char* p) {
  for (int i = 0; i < 12; i++) {
    if ((p[0] == MONTHS[i][0] || p[0] == (MONTHS[i][0] | 0x20)) && (p[1] | 0x20) == (MONTHS[i][1] | 0x20) &&
        (p[2] | 0x20) == (MONTHS[i][2] | 0x20)) {
      return i + 1;
    }
  }
  return 0;
}

// Days from 1970-01-01 for a civil date (Howard Hinnant's days_from_civil).
int64_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u + day - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
}

// Named zones still seen in RSS. Everything unknown is treated as UTC, which is
// what the RFC itself prescribes for unrecognised alphabetic zones.
struct NamedZone {
  const char* name;
  int32_t offsetSec;
};
constexpr NamedZone NAMED_ZONES[] = {
    {"GMT", 0},         {"UT", 0},          {"Z", 0},           {"UTC", 0},
    {"EST", -5 * 3600}, {"EDT", -4 * 3600}, {"CST", -6 * 3600}, {"CDT", -5 * 3600},
    {"MST", -7 * 3600}, {"MDT", -6 * 3600}, {"PST", -8 * 3600}, {"PDT", -7 * 3600},
};

// Parses "+0300", "+03:00", "GMT", "PDT", ... Returns false when nothing
// date-like follows, in which case the caller keeps UTC.
bool parseZone(const char* p, int32_t& offsetSec) {
  p = skipSpaces(p);
  if (*p == '+' || *p == '-') {
    const int sign = (*p == '-') ? -1 : 1;
    p++;
    int hours = 0;
    const char* after = readNumber(p, 2, 2, hours);
    if (!after) return false;
    p = after;
    if (*p == ':') p++;
    int minutes = 0;
    after = readNumber(p, 2, 2, minutes);
    if (!after) minutes = 0;
    offsetSec = sign * (hours * 3600 + minutes * 60);
    return true;
  }
  for (const auto& [name, zoneOffset] : NAMED_ZONES) {
    const size_t len = strlen(name);
    if (strncmp(p, name, len) == 0) {
      offsetSec = zoneOffset;
      return true;
    }
  }
  return false;
}

uint32_t toEpoch(const int year, const int month, const int day, const int hour, const int minute, const int second,
                 const int32_t zoneOffsetSec) {
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
  const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  const int64_t epoch = days * 86400LL + hour * 3600LL + minute * 60LL + second - zoneOffsetSec;
  if (epoch < 0 || epoch > 0xFFFFFFFFLL) return 0;
  return static_cast<uint32_t>(epoch);
}

// "2026-08-25T13:05:00Z", "2026-08-25T13:05:00.123+03:00", also the
// space-separated variant some RDF feeds emit.
uint32_t parseRfc3339(const char* p) {
  int year = 0, month = 0, day = 0;
  p = readNumber(p, 4, 4, year);
  if (!p || *p != '-') return 0;
  p = readNumber(p + 1, 1, 2, month);
  if (!p || *p != '-') return 0;
  p = readNumber(p + 1, 1, 2, day);
  if (!p) return 0;

  int hour = 0, minute = 0, second = 0;
  int32_t zone = 0;
  if (*p == 'T' || *p == 't' || *p == ' ') {
    p = readNumber(p + 1, 1, 2, hour);
    if (!p || *p != ':') return 0;
    p = readNumber(p + 1, 2, 2, minute);
    if (!p) return 0;
    if (*p == ':') {
      p = readNumber(p + 1, 2, 2, second);
      if (!p) return 0;
    }
    if (*p == '.') {  // fractional seconds: skip, we work at second resolution
      p++;
      while (isDigit(*p)) p++;
    }
    parseZone(p, zone);
  }
  return toEpoch(year, month, day, hour, minute, second, zone);
}

// "Mon, 25 Aug 2026 13:05:00 +0300"; the leading weekday is optional and some
// feeds emit a 2-digit year.
uint32_t parseRfc822(const char* p) {
  p = skipSpaces(p);
  // Skip an optional "Mon," style weekday.
  if (!isDigit(*p)) {
    const char* comma = strchr(p, ',');
    if (!comma) return 0;
    p = skipSpaces(comma + 1);
  }
  int day = 0;
  p = readNumber(p, 1, 2, day);
  if (!p) return 0;
  p = skipSpaces(p);
  const int month = monthFromName(p);
  if (month == 0) return 0;
  while (*p && *p != ' ') p++;
  p = skipSpaces(p);
  int year = 0;
  p = readNumber(p, 2, 4, year);
  if (!p) return 0;
  if (year < 100) year += (year < 70) ? 2000 : 1900;

  int hour = 0, minute = 0, second = 0;
  int32_t zone = 0;
  p = skipSpaces(p);
  if (isDigit(*p)) {
    p = readNumber(p, 1, 2, hour);
    if (!p || *p != ':') return 0;
    p = readNumber(p + 1, 2, 2, minute);
    if (!p) return 0;
    if (*p == ':') {
      p = readNumber(p + 1, 2, 2, second);
      if (!p) return 0;
    }
    parseZone(p, zone);
  }
  return toEpoch(year, month, day, hour, minute, second, zone);
}

}  // namespace

namespace NewsDate {

uint32_t parseFeedDateToEpoch(const char* s) {
  if (!s) return 0;
  const char* p = skipSpaces(s);
  if (!*p) return 0;
  // Four leading digits followed by '-' can only be ISO; everything else goes
  // down the RFC 822 path, which also tolerates a missing weekday.
  if (isDigit(p[0]) && isDigit(p[1]) && isDigit(p[2]) && isDigit(p[3]) && p[4] == '-') return parseRfc3339(p);
  return parseRfc822(p);
}

uint32_t localOrdinalForEpoch(const uint32_t epochSeconds, const int32_t localOffsetSec) {
  if (epochSeconds < MIN_VALID_EPOCH) return 0;
  const int64_t local = static_cast<int64_t>(epochSeconds) + localOffsetSec;
  if (local < 0) return 0;
  return static_cast<uint32_t>(local / 86400);
}

uint32_t parseFeedDateToLocalOrdinal(const char* s, const int32_t localOffsetSec) {
  return localOrdinalForEpoch(parseFeedDateToEpoch(s), localOffsetSec);
}

}  // namespace NewsDate
