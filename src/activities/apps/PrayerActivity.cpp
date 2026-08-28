#include "PrayerActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MidadAppSettings.h"
#include "activities/apps/PrayerCoordsActivity.h"
#include "activities/apps/PrayerPickActivity.h"
#include "activities/settings/ClockSyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "prayer/PrayerCityTable.h"
#include "prayer/PrayerMethods.h"
#include "prayer/PrayerPure.h"
#include "util/TimeUtils.h"

namespace {
constexpr StrId TIMING_LABELS[PrayerPure::TIMING_COUNT] = {StrId::STR_PRAYER_FAJR,    StrId::STR_PRAYER_SUNRISE,
                                                           StrId::STR_PRAYER_DHUHR,   StrId::STR_PRAYER_ASR,
                                                           StrId::STR_PRAYER_MAGHRIB, StrId::STR_PRAYER_ISHA};
constexpr StrId HIJRI_MONTHS[12] = {StrId::STR_HIJRI_MONTH_1,  StrId::STR_HIJRI_MONTH_2,  StrId::STR_HIJRI_MONTH_3,
                                    StrId::STR_HIJRI_MONTH_4,  StrId::STR_HIJRI_MONTH_5,  StrId::STR_HIJRI_MONTH_6,
                                    StrId::STR_HIJRI_MONTH_7,  StrId::STR_HIJRI_MONTH_8,  StrId::STR_HIJRI_MONTH_9,
                                    StrId::STR_HIJRI_MONTH_10, StrId::STR_HIJRI_MONTH_11, StrId::STR_HIJRI_MONTH_12};
constexpr const char* NO_TIME = "-";
// Gap between the prayer block and the location block.
constexpr int RULE_MARGIN = 8;
}  // namespace

bool PrayerActivity::hasCountry() const { return MIDAD_APP_SETTINGS.prayerCountryIso[0] != '\0'; }

bool PrayerActivity::isCustomLocation() const {
  return strcmp(MIDAD_APP_SETTINGS.prayerCountryIso, MidadAppSettings::PRAYER_CUSTOM_ISO) == 0;
}

bool PrayerActivity::hasLocation() const { return hasCountry() && MIDAD_APP_SETTINGS.prayerHasCity != 0; }

void PrayerActivity::onEnter() {
  Activity::onEnter();
  cursor = 0;
  refresh();
  requestUpdate();
}

void PrayerActivity::refresh() {
  todayOrdinal = TimeUtils::todayOrdinal();
  hasTimes = false;
  hasHijri = false;
  nextIndex = -1;
  minutesToNext = 0;
  if (todayOrdinal == 0 || !hasLocation()) return;

  int year;
  unsigned month, day;
  if (!TimeUtils::getDateFromDayOrdinal(todayOrdinal, year, month, day)) return;

  const int32_t offsetSeconds = TimeUtils::localOffsetSeconds();
  hasTimes = PrayerCalc::compute(year, month, day, MIDAD_APP_SETTINGS.prayerLatE2, MIDAD_APP_SETTINGS.prayerLngE2,
                                 offsetSeconds, PrayerMethods::current(), times);
  hasHijri = PrayerHijri::fromDayOrdinal(static_cast<int32_t>(todayOrdinal), hijri);
  if (!hasTimes) return;

  // halClock.getTime() reports the RTC as it stands -- UTC once NTP has run;
  // the offset is applied by formatTime, not by getTime (HalClock.cpp). The
  // computed timings are local wall-clock, so the comparison has to be too.
  int nowMinutes = -1;
  uint8_t hour = 0, minute = 0;
  if (halClock.getTime(hour, minute)) {
    nowMinutes = PrayerPure::wrapMinutes(hour * 60 + minute + static_cast<int>(offsetSeconds / 60));
  } else if (TimeUtils::isClockValid()) {
    // No RTC on this device (X4): fall back to the system clock.
    nowMinutes = PrayerPure::localMinutesOfDay(static_cast<int64_t>(time(nullptr)), offsetSeconds);
  }
  if (nowMinutes < 0) return;

  nextIndex = PrayerPure::nextPrayerIndex(times.minutes, nowMinutes);
  if (nextIndex >= 0) minutesToNext = times.minutes[nextIndex] - nowMinutes;
}

void PrayerActivity::openCountryPicker() {
  startActivityForResult(std::make_unique<PrayerPickActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    refresh();
    cursor = hasCountry() ? ROW_CITY : ROW_COUNTRY;
    requestUpdate();
  });
}

void PrayerActivity::openCityPicker() {
  const int countryIndex = PrayerCityTable::findCountry(MIDAD_APP_SETTINGS.prayerCountryIso);
  if (isCustomLocation() || countryIndex <= 0) {
    startActivityForResult(std::make_unique<PrayerCoordsActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             refresh();
                             requestUpdate();
                           });
    return;
  }
  startActivityForResult(
      std::make_unique<PrayerPickActivity>(renderer, mappedInput, static_cast<uint16_t>(countryIndex)),
      [this](const ActivityResult&) {
        refresh();
        requestUpdate();
      });
}

int PrayerActivity::buildRows(Row (&rows)[3]) const {
  int count = 0;
  if (needsClock()) rows[count++] = ROW_CLOCK;
  rows[count++] = ROW_COUNTRY;
  if (hasCountry()) rows[count++] = ROW_CITY;
  return count;
}

void PrayerActivity::activateRow() {
  Row rows[3] = {ROW_NONE, ROW_NONE, ROW_NONE};
  const int count = buildRows(rows);
  const Row row = rows[cursor < count ? cursor : 0];
  if (row == ROW_CLOCK) {
    // The shared clock screen owns the WiFi/NTP dance; the prayer app has no
    // business repeating it.
    startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput), [this](const ActivityResult&) {
      refresh();
      cursor = 0;
      requestUpdate();
    });
    return;
  }
  if (row == ROW_COUNTRY) {
    openCountryPicker();
    return;
  }
  openCityPicker();
}

void PrayerActivity::moveCursor(const int delta) {
  Row rows[3] = {ROW_NONE, ROW_NONE, ROW_NONE};
  const int count = buildRows(rows);
  cursor = (cursor + delta + count) % count;
  requestUpdate();
}

bool PrayerActivity::handleArmedButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) confirmArmed = true;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backArmed = true;
  if (confirmArmed && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmArmed = false;
    activateRow();
    return true;
  }
  if (backArmed && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backArmed = false;
    finish();
    return true;
  }
  return false;
}

void PrayerActivity::loop() {
  if (handleArmedButtons()) return;
  // Front buttons walk the rows, matching the pickers they open. The side
  // buttons do the same here: there are never enough rows to page.
  buttonNavigator.onNextRelease([this] { moveCursor(1); });
  buttonNavigator.onPreviousRelease([this] { moveCursor(-1); });
  buttonNavigator.onScrollNextRelease([this] { moveCursor(1); });
  buttonNavigator.onScrollPreviousRelease([this] { moveCursor(-1); });
}

void PrayerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;
  const int rowHeight = GUI.getListRowStep(false);
  const int font = UI_10_FONT_ID;
  const int textOffset = (rowHeight - renderer.getLineHeight(font)) / 2;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PRAYER_TIMES));

  // One row: label left, value right (mirrored for RTL UI languages), the
  // whole band inverted when selected -- the shape GUI.drawList paints, done
  // by hand because only two of these rows are selectable.
  const bool rtl = I18N.isRtl();
  auto drawRow = [&](const int y, const char* label, const char* value, const bool selected, const bool bold) {
    if (selected) renderer.fillRect(0, y, pageWidth, rowHeight);
    const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int labelWidth = renderer.getTextWidth(font, label, style);
    const int valueWidth = value != nullptr ? renderer.getTextWidth(font, value, style) : 0;
    const int labelX = rtl ? pageWidth - sidePadding - labelWidth : sidePadding;
    const int valueX = rtl ? sidePadding : pageWidth - sidePadding - valueWidth;
    renderer.drawText(font, labelX, y + textOffset, label, !selected, style);
    if (value != nullptr && value[0] != '\0') {
      renderer.drawText(font, valueX, y + textOffset, value, !selected, style);
    }
  };

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  char dateBuf[32] = "";
  if (todayOrdinal != 0) TimeUtils::formatDayOrdinal(todayOrdinal, dateBuf, sizeof(dateBuf));
  char hijriBuf[48] = "";
  if (hasHijri) {
    snprintf(hijriBuf, sizeof(hijriBuf), "%u %s %u", static_cast<unsigned>(hijri.day),
             I18N.get(HIJRI_MONTHS[hijri.month - 1]), static_cast<unsigned>(hijri.year));
  }
  drawRow(y, todayOrdinal != 0 ? dateBuf : tr(STR_PRAYER_CLOCK_INVALID), hijriBuf, false, true);
  y += rowHeight;

  // A label that carries the countdown ("Asr   in 42 min") marks the next
  // prayer, which is also the inverted row -- e-ink has no second emphasis
  // worth using here.
  char labelBuf[48];
  char timeBuf[12];
  for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) {
    const char* label = I18N.get(TIMING_LABELS[i]);
    if (i == nextIndex) {
      char countdown[24];
      snprintf(countdown, sizeof(countdown), tr(STR_PRAYER_IN_MINUTES), minutesToNext);
      snprintf(labelBuf, sizeof(labelBuf), "%s   %s", label, countdown);
      label = labelBuf;
    }
    const char* value = NO_TIME;
    if (hasTimes) {
      PrayerPure::formatClock(timeBuf, sizeof(timeBuf), times.minutes[i], SETTINGS.clockFormat == 1);
      value = timeBuf;
    }
    drawRow(y, label, value, i == nextIndex, false);
    y += rowHeight;
  }

  y += RULE_MARGIN;
  renderer.drawLine(sidePadding, y, pageWidth - sidePadding, y);
  y += RULE_MARGIN;

  const char* countryValue = tr(STR_PRAYER_SET_LOCATION);
  if (isCustomLocation()) {
    countryValue = tr(STR_PRAYER_CUSTOM_COORDS);
  } else if (hasCountry()) {
    const int index = PrayerCityTable::findCountry(MIDAD_APP_SETTINGS.prayerCountryIso);
    countryValue = PrayerCityTable::country(static_cast<uint16_t>(std::max(0, index))).name;
  }

  char coords[28];
  const char* cityValue = tr(STR_PRAYER_SELECT_CITY);
  if (MIDAD_APP_SETTINGS.prayerHasCity != 0) {
    if (isCustomLocation()) {
      char lat[14], lng[14];
      PrayerPure::formatCoordinate(lat, sizeof(lat), MIDAD_APP_SETTINGS.prayerLatE2, true);
      PrayerPure::formatCoordinate(lng, sizeof(lng), MIDAD_APP_SETTINGS.prayerLngE2, false);
      snprintf(coords, sizeof(coords), "%s, %s", lat, lng);
      cityValue = coords;
    } else {
      cityValue = MIDAD_APP_SETTINGS.prayerCityName;
    }
  }

  Row rows[3] = {ROW_NONE, ROW_NONE, ROW_NONE};
  const int rowCount = buildRows(rows);
  for (int i = 0; i < rowCount; ++i) {
    const bool selected = i == cursor;
    switch (rows[i]) {
      case ROW_CLOCK:
        drawRow(y, tr(STR_PRAYER_SET_CLOCK), tr(STR_NOT_SET), selected, false);
        break;
      case ROW_COUNTRY:
        drawRow(y, tr(STR_PRAYER_COUNTRY), countryValue, selected, false);
        break;
      case ROW_CITY:
      default:
        drawRow(y, tr(STR_PRAYER_CITY), cityValue, selected, false);
        break;
    }
    y += rowHeight;
  }

  // Footer: what to do next while the location is incomplete, otherwise what
  // produced the numbers -- the convention matters more than the arithmetic,
  // and a user comparing against a mosque schedule needs to see which one.
  const char* infoLeft = tr(STR_PRAYER_CALCULATED);
  const char* infoRight = "";
  if (needsClock()) {
    infoLeft = tr(STR_PRAYER_CLOCK_INVALID);
  } else if (!hasCountry()) {
    infoLeft = tr(STR_PRAYER_HINT_COUNTRY);
  } else if (MIDAD_APP_SETTINGS.prayerHasCity == 0) {
    infoLeft = tr(STR_PRAYER_HINT_CITY);
  } else {
    infoLeft = hasTimes && !times.exact ? tr(STR_PRAYER_APPROXIMATE) : tr(STR_PRAYER_CALCULATED);
    infoRight = I18N.get(PrayerMethods::currentLabel());
  }
  const int infoY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - renderer.getLineHeight(font);
  const int leftWidth = renderer.getTextWidth(font, infoLeft);
  const int rightWidth = renderer.getTextWidth(font, infoRight);
  renderer.drawText(font, rtl ? pageWidth - sidePadding - leftWidth : sidePadding, infoY, infoLeft);
  if (infoRight[0] != '\0') {
    renderer.drawText(font, rtl ? sidePadding : pageWidth - sidePadding - rightWidth, infoY, infoRight);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
