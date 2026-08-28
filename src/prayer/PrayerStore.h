#pragma once

#include <cstddef>
#include <cstdint>

#include "prayer/PrayerPure.h"

// One day's stored timings, copied out of a month file.
struct PrayerDay {
  char times[PrayerPure::TIMING_COUNT][6];  // "HH:MM"
  // Hijri date as numbers, not Aladhan's English month name: that name is a
  // scholarly transliteration ("Safar" with a dot-under S, "Rabi' al-awwal"
  // with a macron and an ayn) whose glyphs the UI fonts do not carry, and it
  // would be untranslatable anyway. The month is named on screen from the
  // translated Hijri month names instead.
  uint8_t hijriDay;
  uint8_t hijriMonth;  // 1-12, 0 when unknown
  uint16_t hijriYear;
};

// Month files on SD: /.crosspoint/prayer/YYYY-MM.json, one per synced month.
// Written from an Aladhan calendar response (~45KB, streamed to a temp file
// and parsed with a filter) down to ~2.3KB per month:
//   {"v":1,"y":2026,"m":9,"lat":-621,"lng":10685,"method":20,
//    "d":[["04:31","05:43","11:52","15:08","17:55","19:05",16,3,1448], ...]}
// -- six timings, then the hijri day, month number and year.
// lat/lng/method are a fingerprint: a file written for a different location or
// method is treated as absent, on top of the explicit clearMonths() the
// settings screens call.
namespace PrayerStore {

constexpr const char* DIR = "/.crosspoint/prayer";
constexpr const char* TMP_PATH = "/.crosspoint/prayer/dl.tmp";

void monthPath(char* buf, size_t len, int year, unsigned month);
bool hasMonth(int year, unsigned month);

// Today's row for the current settings' location/method. False when the month
// is missing, the fingerprint differs, or the row is malformed.
bool loadDay(uint32_t dayOrdinal, PrayerDay& out);

// True when fewer than 7 days remain in the ordinal's month, so a sync should
// also fetch the following month.
bool needsNextMonth(uint32_t dayOrdinal);

// Parses an Aladhan calendar response at tmpPath and writes the compact month
// file. The fingerprint fields are recorded from the arguments.
bool importAladhan(const char* tmpPath, int year, unsigned month, int32_t latE2, int32_t lngE2, uint8_t method);

// Deletes month files older than year/month.
void pruneBefore(int year, unsigned month);
// Deletes every month file (location or method changed).
void clearMonths();

}  // namespace PrayerStore
