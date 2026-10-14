#pragma once

#include <Arduino.h>
#include <Rtc.h>
#include <time.h>

class HalClock;
extern HalClock halClock;  // Singleton

class HalClock {
  bool _available = false;
  mutable Rtc _sdkRtc;
  mutable uint8_t _cachedHour = 0;
  mutable uint8_t _cachedMinute = 0;
  mutable bool _hasCachedTime = false;
  mutable unsigned long _lastPollMs = 0;
  bool _rtcHasDate = false;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // True if an RTC is present on this device
  bool isAvailable() const { return _available; }

  // True if the RTC holds a plausible calendar date, not just a time of day.
  // Firmware before the date was written left the chip ticking on a year-2000
  // stamp, so a device can be "synced" and still have nothing to restore from.
  // Read once at begin() and updated by syncFromNTP().
  bool rtcHasCalendarDate() const { return _rtcHasDate; }

  // Get current hour (0-23) and minute (0-59).
  // Returns false if RTC is not available.
  bool getTime(uint8_t& hour, uint8_t& minute) const;

  // Format time into a caller-provided buffer.
  // 24h mode produces "HH:MM" (needs >=6 bytes); 12h mode produces "H:MM AM"/"HH:MM PM" (needs >=9 bytes).
  // utcOffsetQuarterHoursBiased: biased quarter-hour offset (48 = UTC+0, 0 = UTC-12, 104 = UTC+14).
  // use12Hour: when true, format as 12-hour clock with AM/PM suffix.
  // Returns false if RTC is not available.
  bool formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48, bool use12Hour = false) const;

  // Sync the RTC from an NTP server. Requires WiFi to be connected.
  // Blocks for up to ~5s while waiting for SNTP response.
  // Returns true if the RTC was successfully updated.
  //
  // Debouncing (skip if already synced once) is enforced by the caller, not here,
  // so the HAL stays free of any app-layer settings dependency.
  bool syncFromNTP();

  // True if the system clock (time(nullptr)) looks like a real calendar date rather
  // than the ESP-IDF post-boot default (epoch 0 / Jan 1 1970). Device-independent --
  // works whether or not a DS3231 is present.
  static bool isSystemTimeValid();

  // Earliest date treated as a real one, shared by isSystemTimeValid() and the
  // restore in begin(): Jan 1 2024, comfortably before this feature existed and
  // comfortably after both the post-boot default (epoch 0) and the year-2000 stamp
  // left by firmware that wrote only hour/minute into the RTC.
  static constexpr time_t MIN_PLAUSIBLE_EPOCH = 1704067200;

  // Sets the system clock via SNTP, independent of any RTC hardware. Blocks for up
  // to ~5s while waiting for SNTP response. Requires WiFi to be connected.
  // Still needed on X4, which has no RTC to preserve the date across a reboot, and on
  // any device whose RTC lost power or was last set by firmware that wrote no calendar
  // date -- without a sync, mbedTLS certificate validation silently fails on every
  // HTTPS request (OPDS, OTA checks, font downloads). Safe/cheap to call whenever
  // isSystemTimeValid() is false; a no-op if WiFi isn't connected yet.
  static bool quickSyncSystemTime();

  // Stops the SNTP client in place, if running. A no-op otherwise. Used when tearing
  // down the network stack without a full chip reset (touch devices skip the reset
  // to keep externally powered peripherals from cycling) -- see
  // finishWifiSessionWithoutRestart() in main.cpp.
  static void stopSntp();

 private:
  // Seeds the system clock from the RTC at boot -- see the definition for why.
  void restoreSystemTimeFromRTC();
  bool writeTimeToRTC(const Rtc::DateTime& dt);
};
