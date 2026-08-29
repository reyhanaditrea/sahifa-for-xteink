#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

HalClock halClock;  // Singleton instance

void HalClock::begin() {
  _available = _sdkRtc.begin();
  LOG_INF("CLK", _available ? "SDK RTC found" : "RTC not found");
  restoreSystemTimeFromRTC();
}

// The RTC keeps running across a reboot; the system clock does not. Without this the
// date is back to 1970 on every boot until something reaches WiFi, which is what makes
// a synced clock look unsynced -- and it silently fails mbedTLS certificate validation
// on every HTTPS request until then. Cheap enough to do unconditionally at boot: one
// I2C read of a chip we have just probed.
void HalClock::restoreSystemTimeFromRTC() {
  if (!_available || isSystemTimeValid()) return;

  Rtc::DateTime dt;
  if (!_sdkRtc.now(dt)) return;  // absent, or oscillator stopped -- nothing trustworthy in there

  struct tm timeinfo = {};
  timeinfo.tm_year = dt.year - 1900;
  timeinfo.tm_mon = dt.month - 1;
  timeinfo.tm_mday = dt.day;
  timeinfo.tm_hour = dt.hour;
  timeinfo.tm_min = dt.minute;
  timeinfo.tm_sec = dt.second;
  // timegm, not mktime: the RTC holds UTC, and mktime would read it through whatever
  // TZ happens to be set at this point in boot.
  const time_t epoch = timegm(&timeinfo);

  // A device last synced by firmware that wrote only hour/minute is still ticking, but
  // stamped year 2000. Its time-of-day is right and the status bar has always shown it;
  // its date is not, so it must not reach anything that dates a file or checks a
  // certificate. Leaving the system clock invalid keeps the existing NTP-on-connect
  // path firing, which repairs the RTC date on the next sync.
  if (epoch < MIN_PLAUSIBLE_EPOCH) {
    LOG_INF("CLK", "RTC has no calendar date (year %u), leaving system time unset", static_cast<unsigned>(dt.year));
    return;
  }

  const timeval tv = {epoch, 0};
  settimeofday(&tv, nullptr);
  LOG_INF("CLK", "System time restored from RTC: %04u-%02u-%02u %02u:%02u:%02u UTC", static_cast<unsigned>(dt.year),
          static_cast<unsigned>(dt.month), static_cast<unsigned>(dt.day), static_cast<unsigned>(dt.hour),
          static_cast<unsigned>(dt.minute), static_cast<unsigned>(dt.second));
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (!_available) return false;

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS) {
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  Rtc::DateTime dt;
  if (!_sdkRtc.now(dt)) {
    if (!_hasCachedTime) return false;
    _lastPollMs = now;
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }
  _cachedHour = dt.hour;
  _cachedMinute = dt.minute;
  _lastPollMs = now;
  _hasCachedTime = true;
  hour = _cachedHour;
  minute = _cachedMinute;
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  uint8_t h, m;
  if (!getTime(h, m)) return false;

  // Apply UTC offset: convert biased value to signed quarter-hours.
  // Clamp against corrupted persisted values so display time can't drift outside [-12:00, +14:00].
  if (utcOffsetQuarterHoursBiased > 104) utcOffsetQuarterHoursBiased = 104;
  int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetQuarterHours * 15;

  // Wrap around 24 hours
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;

  const int hour24 = totalMinutes / 60;
  const int min = totalMinutes % 60;
  if (use12Hour) {
    const bool pm = hour24 >= 12;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour24, min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (!_available) return false;

  if (!quickSyncSystemTime()) return false;

  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  // The whole date, not just the time of day: it is what lets the next boot restore the
  // system clock from the RTC instead of needing WiFi again.
  Rtc::DateTime dt;
  dt.year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
  dt.month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
  dt.day = static_cast<uint8_t>(timeinfo.tm_mday);
  dt.weekday = static_cast<uint8_t>(timeinfo.tm_wday);
  dt.hour = static_cast<uint8_t>(timeinfo.tm_hour);
  dt.minute = static_cast<uint8_t>(timeinfo.tm_min);
  dt.second = static_cast<uint8_t>(timeinfo.tm_sec);

  if (writeTimeToRTC(dt)) {
    LOG_INF("CLK", "RTC set to %04d-%02d-%02d %02d:%02d:%02d UTC", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
            timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return true;
  }
  return false;
}

bool HalClock::isSystemTimeValid() { return time(nullptr) >= MIN_PLAUSIBLE_EPOCH; }

bool HalClock::quickSyncSystemTime() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Wait for SNTP sync to complete (up to 5 seconds)
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      return true;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}

void HalClock::stopSntp() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
}

bool HalClock::writeTimeToRTC(const Rtc::DateTime& dt) {
  if (!_sdkRtc.set(dt)) return false;
  _lastPollMs = 0;
  _cachedHour = dt.hour;
  _cachedMinute = dt.minute;
  _hasCachedTime = true;
  return true;
}
