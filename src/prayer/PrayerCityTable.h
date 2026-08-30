#pragma once

#include <cstdint>
#include <cstring>

// Row shapes for the generated city table (src/prayer/PrayerCities.h, from
// scripts/gen_prayer_cities.py). Coordinates are fixed-point hundredths of a
// degree (~1 km), which is far finer than prayer-time math needs and keeps a
// row at 12 bytes in flash.
struct PrayerCity {
  int16_t latE2;
  int16_t lngE2;
  uint8_t countryIdx;
  const char* name;
};

struct PrayerCountry {
  char iso[3];
  const char* name;
  uint16_t firstCity;  // index into CITIES; cities are [firstCity, next.firstCity)
};

namespace PrayerCityTable {

// Index of the custom-coordinates pseudo-country (always row 0).
constexpr uint16_t CUSTOM_COUNTRY = 0;

uint16_t countryCount();
const PrayerCountry& country(uint16_t index);
uint16_t cityBegin(uint16_t countryIndex);
uint16_t cityEnd(uint16_t countryIndex);
const PrayerCity& city(uint16_t index);

// Country index for an ISO code, or -1 when unknown. "" resolves to the
// custom-coordinates row.
int findCountry(const char* iso);

}  // namespace PrayerCityTable
