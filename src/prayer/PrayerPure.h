#pragma once

#include <cstddef>
#include <cstdint>

// Small time helpers with no HAL/Arduino dependencies, so they compile in the
// host unit tests (test/prayer_pure) as well as the firmware.
namespace PrayerPure {

// Number of daily timings, in display order: Fajr, Sunrise, Dhuhr, Asr,
// Maghrib, Isha.
constexpr int TIMING_COUNT = 6;

// Wraps a minute count into 0..1439, so a UTC time plus a negative or
// past-midnight offset still lands on a valid minute of the local day.
int wrapMinutes(int minutes);

// Local minute of day for a UTC epoch: what the clock on the wall reads.
int localMinutesOfDay(int64_t epochSeconds, int32_t offsetSeconds);

// Minutes since local midnight for an "HH:MM" string, or -1 when malformed.
int timeToMinutes(const char* hhmm);

// "14:02" / "2:02 PM" for a minute of day (needs 9 bytes for 12-hour).
void formatClock(char* buf, size_t len, int minutesOfDay, bool use12Hour);

// Index of the next timing strictly after nowMinutes, or -1 when every prayer
// for today has passed. Sunrise is skipped: it ends Fajr rather than being a
// prayer of its own, so counting down to it would be wrong.
int nextPrayerIndex(const int minutes[TIMING_COUNT], int nowMinutes);

// Fixed-point coordinate as "S 6.21" / "E 106.85" (needs ~12 bytes).
void formatCoordinate(char* buf, size_t len, int32_t valueE2, bool isLatitude);

}  // namespace PrayerPure
