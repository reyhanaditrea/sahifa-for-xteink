#include "prayer/PrayerMethods.h"

#include <cstring>

#include "MidadAppSettings.h"

namespace PrayerMethods {

namespace {
struct Convention {
  StrId label;
  double fajrAngle;
  double ishaAngle;  // ignored when ishaMinutes > 0
  int ishaMinutes;   // fixed minutes after Maghrib
};

// Angles as each authority publishes them. Auto sits first and carries the
// Muslim World League values as a placeholder; methodFor() replaces them.
constexpr Convention TABLE[] = {
    {StrId::STR_PRAYER_METHOD_AUTO, 18.0, 17.0, 0},        {StrId::STR_PRAYER_METHOD_MWL, 18.0, 17.0, 0},
    {StrId::STR_PRAYER_METHOD_UMM_AL_QURA, 18.5, 0.0, 90}, {StrId::STR_PRAYER_METHOD_ISNA, 15.0, 15.0, 0},
    {StrId::STR_PRAYER_METHOD_EGYPT, 19.5, 17.5, 0},       {StrId::STR_PRAYER_METHOD_KARACHI, 18.0, 18.0, 0},
    {StrId::STR_PRAYER_METHOD_KEMENAG, 20.0, 18.0, 0},     {StrId::STR_PRAYER_METHOD_JAKIM, 20.0, 18.0, 0},
    {StrId::STR_PRAYER_METHOD_SINGAPORE, 20.0, 18.0, 0},   {StrId::STR_PRAYER_METHOD_DIYANET, 18.0, 17.0, 0},
    {StrId::STR_PRAYER_METHOD_DUBAI, 18.2, 18.2, 0},       {StrId::STR_PRAYER_METHOD_KUWAIT, 18.0, 17.5, 0},
    {StrId::STR_PRAYER_METHOD_QATAR, 18.0, 0.0, 90},       {StrId::STR_PRAYER_METHOD_TEHRAN, 17.7, 14.0, 0},
    {StrId::STR_PRAYER_METHOD_RUSSIA, 16.0, 15.0, 0},      {StrId::STR_PRAYER_METHOD_FRANCE, 12.0, 12.0, 0},
    {StrId::STR_PRAYER_METHOD_GULF, 19.5, 0.0, 90},        {StrId::STR_PRAYER_METHOD_MOONSIGHTING, 18.0, 18.0, 0},
};
constexpr uint8_t TABLE_COUNT = sizeof(TABLE) / sizeof(TABLE[0]);

struct CountryMethod {
  char iso[3];
  uint8_t index;  // into TABLE
};
// Which convention a country's own authority uses. Only the entries that
// differ from the Muslim World League default are worth listing.
constexpr CountryMethod BY_COUNTRY[] = {
    {"ID", 6},  {"MY", 7},  {"SG", 8},  {"SA", 2},  {"US", 3},  {"CA", 3},  {"EG", 5},
    {"PK", 5},  {"IN", 5},  {"BD", 5},  {"TR", 9},  {"AE", 10}, {"KW", 11}, {"QA", 12},
    {"IR", 13}, {"RU", 14}, {"FR", 15}, {"BH", 16}, {"OM", 16}, {"YE", 16},
};

// Published local schedules that do not simply print the astronomical time.
// Indonesia's Kemenag jadwal rounds each time up to the whole minute and then
// adds an ihtiyati (safety) margin -- a couple of minutes onto each prayer,
// and a few off sunrise so nobody prays Fajr into daylight. Derived by
// comparing the computation against Kemenag's own published month for
// Jakarta, Medan, Surabaya, Makassar and Jayapura: the offsets below match
// the printed schedule on ~85% of those 155 days and are never more than a
// minute from it.
struct Schedule {
  char iso[3];
  bool roundUp;
  int8_t offsets[PrayerPure::TIMING_COUNT];
};
constexpr Schedule SCHEDULES[] = {
    {"ID", true, {2, -4, 3, 2, 3, 2}},
};

// Countries where Hanafi is the prevailing school, so Auto shows its Asr.
constexpr char HANAFI_COUNTRIES[][3] = {"TR", "PK", "IN", "BD", "AF", "UZ", "TJ", "TM", "KG", "KZ", "AZ"};

const Convention& conventionAt(const uint8_t index) { return TABLE[index < TABLE_COUNT ? index : AUTO_INDEX]; }

uint8_t autoIndexForIso(const char* iso) {
  if (iso != nullptr && iso[0] != '\0') {
    for (const auto& entry : BY_COUNTRY) {
      if (strcmp(entry.iso, iso) == 0) return entry.index;
    }
  }
  return 1;  // Muslim World League
}

uint8_t resolve(const uint8_t index, const char* iso) {
  return index == AUTO_INDEX ? autoIndexForIso(iso) : (index < TABLE_COUNT ? index : 1);
}
}  // namespace

uint8_t count() { return TABLE_COUNT; }

uint8_t asrSchoolCount() { return ASR_SCHOOL_COUNT; }

const std::vector<StrId>& asrSchoolLabels() {
  static const std::vector<StrId> kLabels = {StrId::STR_PRAYER_ASR_AUTO, StrId::STR_PRAYER_ASR_STANDARD,
                                             StrId::STR_PRAYER_ASR_HANAFI};
  return kLabels;
}

StrId label(const uint8_t index) { return conventionAt(index).label; }

const std::vector<StrId>& labels() {
  static const std::vector<StrId> kLabels = [] {
    std::vector<StrId> out;
    out.reserve(TABLE_COUNT);
    for (const auto& convention : TABLE) out.push_back(convention.label);
    return out;
  }();
  return kLabels;
}

uint8_t asrShadowFor(const uint8_t asrSchool, const char* iso) {
  if (asrSchool == ASR_STANDARD) return 1;
  if (asrSchool == ASR_HANAFI) return 2;
  if (iso != nullptr) {
    for (const auto& country : HANAFI_COUNTRIES) {
      if (strcmp(country, iso) == 0) return 2;
    }
  }
  return 1;
}

PrayerCalc::Method methodFor(const uint8_t index, const uint8_t asrSchool, const char* iso) {
  const Convention& convention = conventionAt(resolve(index, iso));
  PrayerCalc::Method method{};
  method.fajrAngle = convention.fajrAngle;
  method.ishaAngle = convention.ishaAngle;
  method.ishaMinutes = convention.ishaMinutes;
  // The Asr school is orthogonal to the method: it decides how far the shadow
  // has to grow, not which twilight angle counts.
  method.asrShadow = asrShadowFor(asrSchool, iso);
  method.roundUp = false;
  for (auto& offset : method.offsets) offset = 0;

  if (iso != nullptr) {
    for (const auto& schedule : SCHEDULES) {
      if (strcmp(schedule.iso, iso) != 0) continue;
      method.roundUp = schedule.roundUp;
      for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) method.offsets[i] = schedule.offsets[i];
      break;
    }
  }
  return method;
}

StrId effectiveLabel(const uint8_t index, const char* iso) { return conventionAt(resolve(index, iso)).label; }

PrayerCalc::Method current() {
  return methodFor(MIDAD_APP_SETTINGS.prayerMethod, MIDAD_APP_SETTINGS.prayerAsrSchool,
                   MIDAD_APP_SETTINGS.prayerCountryIso);
}

StrId currentLabel() { return effectiveLabel(MIDAD_APP_SETTINGS.prayerMethod, MIDAD_APP_SETTINGS.prayerCountryIso); }

}  // namespace PrayerMethods
