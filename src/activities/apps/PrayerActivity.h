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
//   Method                              Auto (by country)
//   Asr                                              Auto
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
  // has no date to compute with, so it needs to be reachable from here rather
  // than buried in Settings.
  //
  // Method and Asr are here for the same reason the location is: they are what
  // you reach for while looking at times that disagree with the mosque down the
  // road, and that comparison happens on this screen -- not in Settings -> Apps,
  // three levels away from the numbers being questioned. The footer still names
  // the convention actually in force, which is what "Auto" resolves to.
  enum Row : uint8_t { ROW_CLOCK, ROW_COUNTRY, ROW_CITY, ROW_METHOD, ROW_ASR, ROW_NONE };
  static constexpr int MAX_ROWS = 5;

  void refresh();
  void openCountryPicker();
  void openCityPicker();
  void activateRow();
  void moveCursor(int delta);
  // See PrayerPickActivity: a click counts only where both halves landed.
  bool handleArmedButtons();
  // The rows on screen right now, in order.
  int buildRows(Row (&rows)[MAX_ROWS]) const;
  // Where `target` sits in that list, for the callers that want to leave the
  // cursor on a particular row. A Row is not its own position: the clock row is
  // only sometimes there and the city row only exists once a country is set, so
  // the two differ by one or two most of the time.
  int rowIndex(Row target) const;

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
