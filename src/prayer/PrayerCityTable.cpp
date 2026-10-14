#include "prayer/PrayerCityTable.h"

#include "prayer/PrayerCities.h"

namespace PrayerCityTable {

// cityEnd() reads COUNTRIES[countryIndex + 1], which for the last country is
// the sentinel row the generator appends (it carries CITY_COUNT). If a
// regenerated table ever drops that row, fail the build here rather than read
// one element past the array on device.
static_assert(sizeof(PrayerCityData::COUNTRIES) / sizeof(PrayerCityData::COUNTRIES[0]) ==
                  static_cast<size_t>(PrayerCityData::COUNTRY_COUNT) + 1,
              "PrayerCities.h must carry exactly one sentinel row after the countries");
static_assert(PrayerCityData::COUNTRIES[PrayerCityData::COUNTRY_COUNT].firstCity == PrayerCityData::CITY_COUNT,
              "the sentinel row must carry the total city count");

uint16_t countryCount() { return PrayerCityData::COUNTRY_COUNT; }

const PrayerCountry& country(const uint16_t index) {
  return PrayerCityData::COUNTRIES[index < PrayerCityData::COUNTRY_COUNT ? index : 0];
}

uint16_t cityBegin(const uint16_t countryIndex) { return country(countryIndex).firstCity; }

uint16_t cityEnd(const uint16_t countryIndex) {
  if (countryIndex >= PrayerCityData::COUNTRY_COUNT) return 0;
  // The sentinel row after the last country holds CITY_COUNT.
  return PrayerCityData::COUNTRIES[countryIndex + 1].firstCity;
}

const PrayerCity& city(const uint16_t index) {
  return PrayerCityData::CITIES[index < PrayerCityData::CITY_COUNT ? index : 0];
}

int findCountry(const char* iso) {
  if (iso == nullptr) return -1;
  for (uint16_t i = 0; i < PrayerCityData::COUNTRY_COUNT; ++i) {
    if (strcmp(PrayerCityData::COUNTRIES[i].iso, iso) == 0) return i;
  }
  return -1;
}

}  // namespace PrayerCityTable
