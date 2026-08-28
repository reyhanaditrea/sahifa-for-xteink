#pragma once

#include <I18n.h>

#include <cstdint>
#include <vector>

// Aladhan calculation methods offered in Settings -> Apps -> Prayer Times.
// Index 0 is "Auto (by country)", resolved from the chosen location's ISO
// code; the rest map straight onto Aladhan's `method` query parameter.
namespace PrayerMethods {

struct Method {
  uint8_t aladhanId;  // 0xFF = auto
  StrId label;
};

constexpr uint8_t AUTO_INDEX = 0;
constexpr uint8_t AUTO_ID = 0xFF;

uint8_t count();
const Method& method(uint8_t index);
// Labels in table order, for SettingInfo::DynamicEnum.
const std::vector<StrId>& labels();

// Aladhan method id for a country: Kemenag for Indonesia, Umm al-Qura for
// Saudi Arabia, ISNA for North America, ... and MWL (3) for everyone else,
// including custom coordinates (iso "").
uint8_t autoMethodForIso(const char* iso);

// The id to send for the current settings (method index + location).
uint8_t effectiveAladhanId();

}  // namespace PrayerMethods
