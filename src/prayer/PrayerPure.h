#pragma once

#include <cstddef>
#include <cstdint>

// Prayer Times helpers with no HAL/Arduino dependencies, so they compile in
// the host unit tests (test/prayer_pure) as well as the firmware.
namespace PrayerPure {

// Number of daily timings stored per day, in display order.
constexpr int TIMING_COUNT = 6;
// Aladhan's key for each stored timing, same order as PrayerDay::times.
extern const char* const TIMING_KEYS[TIMING_COUNT];

// Aladhan monthly calendar URL for the given coordinates and method.
// Coordinates are hundredths of a degree; formatted without float printf.
void buildCalendarUrl(char* buf, size_t len, int year, unsigned month, int32_t latE2, int32_t lngE2, uint8_t method);

// Copies the "HH:MM" prefix of an Aladhan timing ("04:31 (WIB)") into out
// (needs 6 bytes). Returns false when the input does not start with HH:MM.
bool copyTime(const char* in, char* out, size_t outLen);

// Minutes since local midnight for an "HH:MM" string, or -1 when malformed.
int timeToMinutes(const char* hhmm);

// Index of the next timing strictly after nowMinutes (Sunrise excluded, since
// it is not a prayer), or -1 when every prayer for today has passed.
int nextPrayerIndex(const char times[TIMING_COUNT][6], int nowMinutes);

// Parses "YYYY-MM.json" (a stored month file name). Returns false otherwise.
bool parseMonthFileName(const char* name, int& year, unsigned& month);

// Fixed-point coordinate as "S 6.21" / "E 106.85" (needs ~12 bytes).
void formatCoordinate(char* buf, size_t len, int32_t valueE2, bool isLatitude);

}  // namespace PrayerPure
