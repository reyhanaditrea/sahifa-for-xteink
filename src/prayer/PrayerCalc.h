#pragma once

#include <cstdint>

#include "prayer/PrayerPure.h"

// Prayer times computed on the device. There is nothing to fetch: given a
// date, a position and a calculation method, the timings follow from the
// sun's position, which is the same arithmetic any prayer-times API runs
// server-side. Doing it here means no network, no stored months, and a
// countdown that cannot disagree with the times it counts down to.
//
// The algorithm is the standard one (Meeus' solar position, then the hour
// angle at each method's twilight depression): midday from the equation of
// time, sunrise/sunset at -0.833 degrees for refraction and the sun's radius,
// Fajr and Isha at their method angles, Asr from the shadow ratio.
namespace PrayerCalc {

struct Method {
  double fajrAngle;  // degrees below the horizon
  double ishaAngle;  // degrees; ignored when ishaMinutes > 0
  int ishaMinutes;   // fixed minutes after Maghrib (Umm al-Qura), else 0
  int asrShadow;     // 1 = Shafi/Maliki/Hanbali, 2 = Hanafi
  // Published schedules round a time up to the whole minute rather than to
  // the nearest one, so a prayer is never announced before its computed
  // instant. Kemenag's jadwal does; the plain astronomical convention (and
  // every prayer-times API) rounds to nearest.
  bool roundUp;
  // Minutes added per timing after rounding, in PrayerPure::TIMING_KEYS order.
  // Indonesia's published schedule carries an ihtiyati (safety) margin, so the
  // device matches the printed jadwal rather than the bare astronomy.
  int8_t offsets[PrayerPure::TIMING_COUNT];
};

struct DayTimes {
  // Local minutes since midnight, in PrayerPure::TIMING_KEYS order.
  int minutes[PrayerPure::TIMING_COUNT];
  // Unrounded local minutes, before rounding and the method's offsets: the
  // rounding rule is part of matching a published schedule, so keep the input
  // to it available for the tests that check that match.
  double rawMinutes[PrayerPure::TIMING_COUNT];
  // False when the sun never reaches a twilight angle that day (high
  // latitudes): the timings then come from the nearest-latitude fallback and
  // are an approximation, not an observation.
  bool exact;
};

// latE2/lngE2 are hundredths of a degree; utcOffsetSeconds is the local offset
// the times should be expressed in. Returns false only for an impossible date.
bool compute(int year, unsigned month, unsigned day, int32_t latE2, int32_t lngE2, int32_t utcOffsetSeconds,
             const Method& method, DayTimes& out);

}  // namespace PrayerCalc
