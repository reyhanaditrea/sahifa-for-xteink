#include "prayer/PrayerCalc.h"

#include <cmath>

namespace PrayerCalc {

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;

double toRad(const double degrees) { return degrees * DEG; }
double toDeg(const double radians) { return radians / DEG; }

double wrap(const double value, const double range) {
  double v = fmod(value, range);
  if (v < 0) v += range;
  return v;
}

// Julian day for a civil date at 00:00 UT (Meeus, chapter 7).
double julianDay(int year, int month, const int day) {
  if (month <= 2) {
    year -= 1;
    month += 12;
  }
  const double a = floor(year / 100.0);
  const double b = 2 - a + floor(a / 4.0);
  return floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + b - 1524.5;
}

struct SunPosition {
  double declination;     // degrees
  double equationOfTime;  // hours
};

// Low-precision solar coordinates, good to well under the minute that a
// printed prayer schedule resolves to.
SunPosition sunPosition(const double jd) {
  const double d = jd - 2451545.0;
  const double meanAnomaly = wrap(357.529 + 0.98560028 * d, 360.0);
  const double meanLongitude = wrap(280.459 + 0.98564736 * d, 360.0);
  const double eclipticLongitude =
      wrap(meanLongitude + 1.915 * sin(toRad(meanAnomaly)) + 0.020 * sin(toRad(2 * meanAnomaly)), 360.0);
  const double obliquity = 23.439 - 0.00000036 * d;

  const double declination = toDeg(asin(sin(toRad(obliquity)) * sin(toRad(eclipticLongitude))));
  double rightAscension =
      toDeg(atan2(cos(toRad(obliquity)) * sin(toRad(eclipticLongitude)), cos(toRad(eclipticLongitude)))) / 15.0;
  rightAscension = wrap(rightAscension, 24.0);
  const double equationOfTime = meanLongitude / 15.0 - rightAscension;
  return {declination, wrap(equationOfTime + 12.0, 24.0) - 12.0};
}

// Hours from midday to the moment the sun sits `angle` degrees below the
// horizon. NaN when it never does (polar summer or winter).
double hourAngle(const double angle, const double latitude, const double declination) {
  const double numerator = -sin(toRad(angle)) - sin(toRad(declination)) * sin(toRad(latitude));
  const double denominator = cos(toRad(declination)) * cos(toRad(latitude));
  const double ratio = numerator / denominator;
  if (ratio > 1.0 || ratio < -1.0) return NAN;
  return toDeg(acos(ratio)) / 15.0;
}

// The sun's depression when a stick's shadow has grown by `shadow` times its
// own length beyond the shadow it cast at midday.
double asrAngle(const int shadow, const double latitude, const double declination) {
  const double atNoon = fabs(latitude - declination);
  return -toDeg(atan(1.0 / (shadow + tan(toRad(atNoon)))));
}

int roundToMinute(const double minutes, const bool roundUp) {
  return static_cast<int>(roundUp ? ceil(minutes) : floor(minutes + 0.5));
}
}  // namespace

bool compute(const int year, const unsigned month, const unsigned day, const int32_t latE2, const int32_t lngE2,
             const int32_t utcOffsetSeconds, const Method& method, DayTimes& out) {
  if (month < 1 || month > 12 || day < 1 || day > 31) return false;

  const double latitude = latE2 / 100.0;
  const double longitude = lngE2 / 100.0;
  const double offsetHours = utcOffsetSeconds / 3600.0;
  // The day starts at local midnight, which is a different instant from UT
  // midnight everywhere but Greenwich.
  const double jdBase = julianDay(year, static_cast<int>(month), static_cast<int>(day)) - offsetHours / 24.0;

  out.exact = true;

  // One refinement pass: the sun moves while the day runs, so recompute its
  // position at each timing's own moment rather than at midnight.
  double times[PrayerPure::TIMING_COUNT] = {5.0, 6.0, 12.0, 13.0, 18.0, 19.0};  // hours, rough seeds
  for (int pass = 0; pass < 2; ++pass) {
    const SunPosition noon = sunPosition(jdBase + times[2] / 24.0);
    const double midday = 12.0 - noon.equationOfTime - longitude / 15.0 + offsetHours;

    for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) {
      const SunPosition sun = sunPosition(jdBase + times[i] / 24.0);
      const double middayForThis = 12.0 - sun.equationOfTime - longitude / 15.0 + offsetHours;
      switch (i) {
        case 0: {  // Fajr
          const double h = hourAngle(method.fajrAngle, latitude, sun.declination);
          times[i] = std::isnan(h) ? NAN : middayForThis - h;
          break;
        }
        case 1: {  // Sunrise
          const double h = hourAngle(0.833, latitude, sun.declination);
          times[i] = std::isnan(h) ? NAN : middayForThis - h;
          break;
        }
        case 2:  // Dhuhr
          times[i] = midday;
          break;
        case 3: {  // Asr
          const double h = hourAngle(asrAngle(method.asrShadow, latitude, sun.declination), latitude, sun.declination);
          times[i] = std::isnan(h) ? NAN : middayForThis + h;
          break;
        }
        case 4: {  // Maghrib, at sunset
          const double h = hourAngle(0.833, latitude, sun.declination);
          times[i] = std::isnan(h) ? NAN : middayForThis + h;
          break;
        }
        default: {  // Isha
          if (method.ishaMinutes > 0) {
            times[i] = times[4] + method.ishaMinutes / 60.0;
          } else {
            const double h = hourAngle(method.ishaAngle, latitude, sun.declination);
            times[i] = std::isnan(h) ? NAN : middayForThis + h;
          }
          break;
        }
      }
      // Keep a usable seed for the next pass even where the angle is never
      // reached, or the refinement would spread the NaN to everything.
      if (std::isnan(times[i])) {
        out.exact = false;
        times[i] = 12.0 + (i < 2 ? -6.0 : 6.0);
      }
    }
  }

  // High latitudes: where a twilight angle is never reached, fall back to the
  // "one seventh of the night" rule, the same shape of approximation an API
  // applies silently. Marked inexact so the screen can say so.
  if (!out.exact) {
    const SunPosition sun = sunPosition(jdBase + 0.5);
    const double sunriseHours = hourAngle(0.833, latitude, sun.declination);
    const double midday = 12.0 - sun.equationOfTime - longitude / 15.0 + offsetHours;
    const double daylight = std::isnan(sunriseHours) ? 12.0 : 2.0 * sunriseHours;
    const double night = 24.0 - daylight;
    const double sunrise = midday - daylight / 2.0;
    const double sunset = midday + daylight / 2.0;
    if (std::isnan(times[1])) times[1] = sunrise;
    if (std::isnan(times[4])) times[4] = sunset;
    if (std::isnan(times[0])) times[0] = sunrise - night / 7.0;
    if (std::isnan(times[5])) times[5] = sunset + night / 7.0;
    if (std::isnan(times[3])) times[3] = midday + daylight / 4.0;
  }

  for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) {
    out.rawMinutes[i] = times[i] * 60.0;
    out.minutes[i] = PrayerPure::wrapMinutes(roundToMinute(out.rawMinutes[i], method.roundUp) + method.offsets[i]);
  }
  return true;
}

}  // namespace PrayerCalc
