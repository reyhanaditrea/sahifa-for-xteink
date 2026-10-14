#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Full-page picker for the Prayer Times app: the country, the cities inside
// it (over the generated table in src/prayer/PrayerCities.h), the calculation
// method, or the Asr school. One class for all four -- they differ only in
// what they enumerate and what a pick writes.
//
// A full page rather than Settings' option popup: that popup sizes itself to
// its option count and does not scroll, so eighteen conventions run off the
// panel. Font Family and Arabic Font take the same way out. The Asr list is
// three rows and would fit the popup, but it opens from the row directly under
// the method on the same screen, and two different shapes for two adjacent
// rows is a seam worth not having.
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
  // Asr school picker (Auto / Standard / Hanafi).
  struct AsrTag {};
  PrayerPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, AsrTag)
      : Activity("PrayerPickAsr", renderer, mappedInput), countryIndex(0), mode(Mode::AsrSchool) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode : uint8_t { Country, City, Method, AsrSchool };

  int itemCount() const;
  void activate();
  // A button counts only when this screen saw both its press and its release.
  // Settings dispatches on the press, so without this the release of the very
  // click that opened the picker selected whatever row happened to be
  // highlighted and closed it again -- the stray-click bug the interval
  // picker had.
  bool handleArmedButtons();

  const uint16_t countryIndex;
  const Mode mode;
  int selectedIndex = 0;
  int itemsPerPage = 1;
  bool confirmArmed = false;
  bool backArmed = false;
  ButtonNavigator buttonNavigator;
};
