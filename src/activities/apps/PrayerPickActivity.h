#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Country or city picker for the Prayer Times app, over the generated table
// in src/prayer/PrayerCities.h. One class for both levels: the lists differ
// only in what they enumerate and what a pick writes.
//
// Picking writes MidadAppSettings and clears the stored months (Aladhan
// computed those for the old coordinates), then finishes -- PrayerActivity
// re-reads the settings in its result handler.
class PrayerPickActivity final : public Activity {
 public:
  // Country picker (row 0 is "Custom coordinates").
  PrayerPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PrayerPickCountry", renderer, mappedInput), countryIndex(0), mode(Mode::Country) {}
  // City picker for one country.
  PrayerPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint16_t country)
      : Activity("PrayerPickCity", renderer, mappedInput), countryIndex(country), mode(Mode::City) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode : uint8_t { Country, City };

  int itemCount() const;
  void activate();

  const uint16_t countryIndex;
  const Mode mode;
  int selectedIndex = 0;
  int itemsPerPage = 1;
  ButtonNavigator buttonNavigator;
};
