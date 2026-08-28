#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Manual latitude/longitude entry, used when the city table has nothing near
// enough (the "Custom coordinates" row at the top of the country picker).
// Same shape as ClockOffsetActivity: Confirm cycles the field, Up/Down adjust
// it (held = continuous), Back saves and leaves.
class PrayerCoordsActivity final : public Activity {
 public:
  PrayerCoordsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PrayerCoords", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Degrees and hundredths are separate fields: one Up press should move a
  // whole degree (~111 km) or a hundredth (~1 km), never a blend of both.
  enum Field : uint8_t {
    FIELD_LAT_DEGREES = 0,
    FIELD_LAT_FRACTION,
    FIELD_LNG_DEGREES,
    FIELD_LNG_FRACTION,
    FIELD_COUNT,
  };

  void adjust(int delta);

  Field activeField = FIELD_LAT_DEGREES;
  // Working copy in hundredths of a degree, seeded from settings.
  int32_t latE2 = 0;
  int32_t lngE2 = 0;
  ButtonNavigator buttonNavigator;
};
