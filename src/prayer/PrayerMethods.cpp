#include "prayer/PrayerMethods.h"

#include <cstring>

#include "MidadAppSettings.h"

namespace PrayerMethods {

namespace {
constexpr Method TABLE[] = {
    {AUTO_ID, StrId::STR_PRAYER_METHOD_AUTO},  {3, StrId::STR_PRAYER_METHOD_MWL},
    {4, StrId::STR_PRAYER_METHOD_UMM_AL_QURA}, {2, StrId::STR_PRAYER_METHOD_ISNA},
    {5, StrId::STR_PRAYER_METHOD_EGYPT},       {1, StrId::STR_PRAYER_METHOD_KARACHI},
    {20, StrId::STR_PRAYER_METHOD_KEMENAG},    {17, StrId::STR_PRAYER_METHOD_JAKIM},
    {11, StrId::STR_PRAYER_METHOD_SINGAPORE},  {13, StrId::STR_PRAYER_METHOD_DIYANET},
    {16, StrId::STR_PRAYER_METHOD_DUBAI},      {9, StrId::STR_PRAYER_METHOD_KUWAIT},
    {10, StrId::STR_PRAYER_METHOD_QATAR},      {7, StrId::STR_PRAYER_METHOD_TEHRAN},
    {14, StrId::STR_PRAYER_METHOD_RUSSIA},     {12, StrId::STR_PRAYER_METHOD_FRANCE},
    {8, StrId::STR_PRAYER_METHOD_GULF},        {15, StrId::STR_PRAYER_METHOD_MOONSIGHTING},
};
constexpr uint8_t TABLE_COUNT = sizeof(TABLE) / sizeof(TABLE[0]);

struct CountryMethod {
  char iso[3];
  uint8_t aladhanId;
};
constexpr CountryMethod BY_COUNTRY[] = {
    {"ID", 20}, {"SA", 4},  {"US", 2},  {"CA", 2},  {"EG", 5}, {"PK", 1},  {"IN", 1}, {"BD", 1},
    {"TR", 13}, {"MY", 17}, {"SG", 11}, {"AE", 16}, {"KW", 9}, {"QA", 10}, {"IR", 7}, {"RU", 14},
    {"FR", 12}, {"BH", 8},  {"OM", 8},  {"YE", 8},  {"IQ", 8}, {"SY", 8},  {"JO", 8}, {"LB", 8},
};
constexpr uint8_t DEFAULT_METHOD = 3;  // Muslim World League
}  // namespace

uint8_t count() { return TABLE_COUNT; }

const Method& method(const uint8_t index) { return TABLE[index < TABLE_COUNT ? index : AUTO_INDEX]; }

const std::vector<StrId>& labels() {
  static const std::vector<StrId> kLabels = [] {
    std::vector<StrId> out;
    out.reserve(TABLE_COUNT);
    for (const auto& m : TABLE) out.push_back(m.label);
    return out;
  }();
  return kLabels;
}

uint8_t autoMethodForIso(const char* iso) {
  if (iso == nullptr || iso[0] == '\0') return DEFAULT_METHOD;
  for (const auto& entry : BY_COUNTRY) {
    if (strcmp(entry.iso, iso) == 0) return entry.aladhanId;
  }
  return DEFAULT_METHOD;
}

uint8_t effectiveAladhanId() {
  const Method& m = method(MIDAD_APP_SETTINGS.prayerMethod);
  if (m.aladhanId != AUTO_ID) return m.aladhanId;
  return autoMethodForIso(MIDAD_APP_SETTINGS.prayerCountryIso);
}

}  // namespace PrayerMethods
