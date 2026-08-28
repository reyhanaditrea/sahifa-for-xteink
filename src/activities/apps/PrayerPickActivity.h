#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Full-page picker for the Prayer Times app: the country, the cities inside
// it (over the generated table in src/prayer/PrayerCities.h), or the
// calculation method. One class for all three -- they differ only in what
// they enumerate and what a pick writes.
//
// The method list is here rather than in Settings' option popup because that
// popup sizes itself to its option count with no scrolling, so eighteen
// conventions run off the panel. Font Family and Arabic Font take the same
// way out (see SettingsActivity::toggleCurrentSetting).
//
// Picking writes MidadAppSettings and finishes; the caller re-reads the
// settings in its result handler.
class PrayerPickActivity final : public Activity {
 public:
  // Country picker (row 0 is "Custom coordinates").
  PrayerPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PrayerPickCountry", renderer, mappedInput), countryIndex(0), mode(Mode::Country) {}
  // City picker for one country.
  PrayerPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint16_t country)
      : Activity("PrayerPickCity", renderer, mappedInput), countryIndex(country), mode(Mode::City) {}
  // Calculation method picker.
  struct MethodTag {};
  PrayerPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, MethodTag)
      : Activity("PrayerPickMethod", renderer, mappedInput), countryIndex(0), mode(Mode::Method) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode : uint8_t { Country, City, Method };

  int itemCount() const;
  void activate();

  const uint16_t countryIndex;
  const Mode mode;
  int selectedIndex = 0;
  int itemsPerPage = 1;
  ButtonNavigator buttonNavigator;
};
