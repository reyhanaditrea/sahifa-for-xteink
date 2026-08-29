#pragma once

#include <I18n.h>

#include <cstdint>
#include <vector>

#include "prayer/PrayerCalc.h"

// The calculation conventions offered in Settings -> Apps -> Prayer Times.
// A "method" is nothing more than the twilight angles a given authority
// settles on, which is why the times can be computed on the device: there is
// no schedule to fetch, only these few numbers.
//
// Index 0 is "Auto (by country)", resolved from the chosen location's ISO
// code. The country also decides whether the published local schedule carries
// an ihtiyati (safety) margin -- Indonesia's does -- see scheduleFor().
namespace PrayerMethods {

constexpr uint8_t AUTO_INDEX = 0;

// Asr school setting values.
constexpr uint8_t ASR_AUTO = 0;
constexpr uint8_t ASR_STANDARD = 1;
constexpr uint8_t ASR_HANAFI = 2;
constexpr uint8_t ASR_SCHOOL_COUNT = 3;

uint8_t count();
StrId label(uint8_t index);

// The Asr schools, in settings order: Auto, Standard (Shafi/Maliki/Hanbali),
// Hanafi. Auto follows the country's prevailing school -- Hanafi across
// Turkiye, South and Central Asia -- and the majority position elsewhere.
uint8_t asrSchoolCount();
const std::vector<StrId>& asrSchoolLabels();

// The angles for a method index, resolving Auto through the country's ISO
// code (Kemenag for Indonesia, Umm al-Qura for Saudi Arabia, ISNA for North
// America, MWL for everyone else, including custom coordinates). asrSchool
// indexes the list above.
PrayerCalc::Method methodFor(uint8_t index, uint8_t asrSchool, const char* iso);

// The label actually in force, so the screen can name what it computed with
// even when the setting says "Auto".
StrId effectiveLabel(uint8_t index, const char* iso);

// The method the current settings ask for.
PrayerCalc::Method current();
StrId currentLabel();

}  // namespace PrayerMethods
