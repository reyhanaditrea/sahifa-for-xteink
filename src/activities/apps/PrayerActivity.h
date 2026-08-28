#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "prayer/PrayerStore.h"
#include "util/ButtonNavigator.h"

// Today's prayer times for the chosen city, plus the two location rows and
// the sync row -- one screen, drawn in the Settings list idiom (single body
// font, one row height, inverted selection).
//
//   Friday, 28 Aug 2026          15 Rabi Al-Awwal 1448
//   Fajr                                         04:38   ("-" until synced)
//   ...
//   Asr   in 42 min                              15:14   (next prayer, inverted)
//   ----------------------------------------------------
//   Country                                  Indonesia
//   City                                       Jakarta   (hidden until a country is set)
//   Sync Now                         Last synced 14:02
//
//   Pick a country to get started / Source: Aladhan
//
// Not a UiListActivity: only the bottom three rows are selectable, the rest
// are read-out, and that base assumes every row is a target.
class PrayerActivity final : public Activity {
 public:
  PrayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Prayer", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // The selectable rows, in screen order. City is skipped while no country is
  // set (the row is not drawn either).
  enum Row : uint8_t { ROW_COUNTRY = 0, ROW_CITY = 1, ROW_SYNC = 2, ROW_COUNT = 3 };

  void refresh();
  void openCountryPicker();
  void openCityPicker();
  void startSync();
  void activateRow();
  void moveCursor(int delta);

  bool hasCountry() const;
  bool isCustomLocation() const;
  bool canSync() const;
  int visibleRowCount() const { return hasCountry() ? ROW_COUNT : ROW_COUNT - 1; }
  Row rowAt(int cursor) const;

  PrayerDay day{};
  bool hasDay = false;
  // Index into the timings of the next prayer today, or -1 (all passed, no
  // data, or the clock is not set).
  int nextIndex = -1;
  int minutesToNext = 0;
  uint32_t todayOrdinal = 0;

  int cursor = 0;
  ButtonNavigator buttonNavigator;
};
