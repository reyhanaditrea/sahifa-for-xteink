#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "prayer/PrayerCalc.h"
#include "prayer/PrayerHijri.h"
#include "util/ButtonNavigator.h"

// Today's prayer times for the chosen city, computed on the device.
//
//   Friday, 28 Aug 2026          15 Rabi al-awwal 1448
//   Fajr                                         04:39
//   Sunrise                                      05:52
//   Dhuhr                                        11:58
//   Asr    in 42 min                             15:16   (next prayer, inverted)
//   Maghrib                                      17:56
//   Isha                                         19:06
//   ----------------------------------------------------
//   Country                                  Indonesia
//   City                                       Jakarta   (hidden until a country is set)
//
//   Calculated on device               Kemenag Indonesia
//
// There is nothing to fetch: the timings follow from the sun's position at
// the city's coordinates (PrayerCalc), so the screen is never empty for want
// of a network, and the countdown cannot disagree with the times above it.
//
// Not a UiListActivity: only the two location rows are selectable, and that
// base assumes every row is a target.
class PrayerActivity final : public Activity {
 public:
  PrayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Prayer", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // The rows a cursor can sit on. The clock row only appears when the device
  // has no date to compute with -- which is the normal state on hardware
  // whose RTC does not keep one across a reboot, so it needs to be reachable
  // from here rather than buried in Settings.
  enum Row : uint8_t { ROW_CLOCK, ROW_COUNTRY, ROW_CITY, ROW_NONE };

  void refresh();
  void openCountryPicker();
  void openCityPicker();
  void activateRow();
  void moveCursor(int delta);
  // See PrayerPickActivity: a click counts only where both halves landed.
  bool handleArmedButtons();
  // The rows on screen right now, in order.
  int buildRows(Row (&rows)[3]) const;

  bool hasCountry() const;
  bool isCustomLocation() const;
  bool hasLocation() const;
  bool needsClock() const { return todayOrdinal == 0; }

  PrayerCalc::DayTimes times{};
  bool hasTimes = false;
  PrayerHijri::HijriDate hijri{};
  bool hasHijri = false;
  // Index of the next prayer today, or -1 (all passed, no location, or the
  // clock has never been set).
  int nextIndex = -1;
  int minutesToNext = 0;
  uint32_t todayOrdinal = 0;

  int cursor = 0;
  ButtonNavigator buttonNavigator;
  bool confirmArmed = false;
  bool backArmed = false;
};
