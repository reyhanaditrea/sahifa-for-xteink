#include "PrayerActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MidadAppSettings.h"
#include "activities/apps/PrayerCoordsActivity.h"
#include "activities/apps/PrayerPickActivity.h"
#include "activities/apps/PrayerSyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "prayer/PrayerCityTable.h"
#include "prayer/PrayerPure.h"
#include "util/TimeUtils.h"

namespace {
constexpr StrId TIMING_LABELS[PrayerPure::TIMING_COUNT] = {StrId::STR_PRAYER_FAJR,    StrId::STR_PRAYER_SUNRISE,
                                                           StrId::STR_PRAYER_DHUHR,   StrId::STR_PRAYER_ASR,
                                                           StrId::STR_PRAYER_MAGHRIB, StrId::STR_PRAYER_ISHA};
constexpr const char* NO_TIME = "-";
constexpr StrId HIJRI_MONTHS[12] = {StrId::STR_HIJRI_MONTH_1,  StrId::STR_HIJRI_MONTH_2,  StrId::STR_HIJRI_MONTH_3,
                                    StrId::STR_HIJRI_MONTH_4,  StrId::STR_HIJRI_MONTH_5,  StrId::STR_HIJRI_MONTH_6,
                                    StrId::STR_HIJRI_MONTH_7,  StrId::STR_HIJRI_MONTH_8,  StrId::STR_HIJRI_MONTH_9,
                                    StrId::STR_HIJRI_MONTH_10, StrId::STR_HIJRI_MONTH_11, StrId::STR_HIJRI_MONTH_12};
// Gap between the prayer block and the location block.
constexpr int RULE_MARGIN = 8;
}  // namespace

bool PrayerActivity::hasCountry() const { return MIDAD_APP_SETTINGS.prayerCountryIso[0] != '\0'; }

bool PrayerActivity::isCustomLocation() const {
  return strcmp(MIDAD_APP_SETTINGS.prayerCountryIso, MidadAppSettings::PRAYER_CUSTOM_ISO) == 0;
}

bool PrayerActivity::canSync() const { return hasCountry() && MIDAD_APP_SETTINGS.prayerHasCity != 0; }

PrayerActivity::Row PrayerActivity::rowAt(const int index) const {
  // Without a country the City row is absent, so the second visible row is Sync.
  if (!hasCountry() && index >= ROW_CITY) return ROW_SYNC;
  return static_cast<Row>(index);
}

void PrayerActivity::onEnter() {
  Activity::onEnter();
  cursor = 0;
  refresh();
  requestUpdate();
}

void PrayerActivity::refresh() {
  todayOrdinal = TimeUtils::todayOrdinal();
  hasDay = todayOrdinal != 0 && PrayerStore::loadDay(todayOrdinal, day);
  nextIndex = -1;
  minutesToNext = 0;
  if (!hasDay) return;

  uint8_t hour = 0, minute = 0;
  if (!halClock.getTime(hour, minute)) return;
  const int nowMinutes = hour * 60 + minute;
  nextIndex = PrayerPure::nextPrayerIndex(day.times, nowMinutes);
  if (nextIndex >= 0) minutesToNext = PrayerPure::timeToMinutes(day.times[nextIndex]) - nowMinutes;
}

void PrayerActivity::openCountryPicker() {
  startActivityForResult(std::make_unique<PrayerPickActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    // The picker writes the settings itself (it also has to clear the stored
    // months); this side just re-reads them.
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

void PrayerActivity::startSync() {
  if (!canSync()) return;
  startActivityForResult(std::make_unique<PrayerSyncActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    refresh();
    requestUpdate();
  });
}

void PrayerActivity::activateRow() {
  switch (rowAt(cursor)) {
    case ROW_COUNTRY:
      openCountryPicker();
      return;
    case ROW_CITY:
      openCityPicker();
      return;
    case ROW_SYNC:
    default:
      startSync();
      return;
  }
}

void PrayerActivity::moveCursor(const int delta) {
  const int count = visibleRowCount();
  cursor = (cursor + delta + count) % count;
  requestUpdate();
}

void PrayerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow();
    return;
  }
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
  // whole band inverted when selected -- the same shape GUI.drawList paints,
  // done by hand because only three of these rows are selectable.
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

  // Date line: gregorian left, hijri right. The hijri date comes from the
  // stored month (Aladhan computes it), so it is blank until a sync.
  char dateBuf[32] = "";
  if (todayOrdinal != 0) TimeUtils::formatDayOrdinal(todayOrdinal, dateBuf, sizeof(dateBuf));
  char hijriBuf[48] = "";
  if (hasDay && day.hijriMonth >= 1 && day.hijriMonth <= 12) {
    snprintf(hijriBuf, sizeof(hijriBuf), "%u %s %u", static_cast<unsigned>(day.hijriDay),
             I18N.get(HIJRI_MONTHS[day.hijriMonth - 1]), static_cast<unsigned>(day.hijriYear));
  }
  drawRow(y, todayOrdinal != 0 ? dateBuf : tr(STR_PRAYER_CLOCK_INVALID), hijriBuf, false, true);
  y += rowHeight;

  // The six timings. A label that carries the countdown ("Asr   in 42 min")
  // marks the next prayer, which is also the inverted row -- e-ink has no
  // second emphasis worth using here.
  char labelBuf[48];
  for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) {
    const char* label = I18N.get(TIMING_LABELS[i]);
    if (i == nextIndex) {
      char countdown[24];
      snprintf(countdown, sizeof(countdown), tr(STR_PRAYER_IN_MINUTES), minutesToNext);
      snprintf(labelBuf, sizeof(labelBuf), "%s   %s", label, countdown);
      label = labelBuf;
    }
    drawRow(y, label, hasDay ? day.times[i] : NO_TIME, i == nextIndex, false);
    y += rowHeight;
  }

  y += RULE_MARGIN;
  renderer.drawLine(sidePadding, y, pageWidth - sidePadding, y);
  y += RULE_MARGIN;

  const int selectedRow = rowAt(cursor);
  drawRow(y, tr(STR_PRAYER_COUNTRY),
          hasCountry()
              ? (isCustomLocation() ? tr(STR_PRAYER_CUSTOM_COORDS)
                                    : PrayerCityTable::country(
                                          static_cast<uint16_t>(std::max(
                                              0, PrayerCityTable::findCountry(MIDAD_APP_SETTINGS.prayerCountryIso))))
                                          .name)
              : tr(STR_PRAYER_SET_LOCATION),
          selectedRow == ROW_COUNTRY, false);
  y += rowHeight;

  if (hasCountry()) {
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
    drawRow(y, tr(STR_PRAYER_CITY), cityValue, selectedRow == ROW_CITY, false);
    y += rowHeight;
  }

  // "Sync Now" carries the last sync time as its value: that is the one thing
  // a user checks before deciding to sync again.
  char syncValue[32] = "";
  if (canSync()) {
    if (MIDAD_APP_SETTINGS.prayerLastSyncEpoch == 0) {
      strlcpy(syncValue, tr(STR_PRAYER_NEVER), sizeof(syncValue));
    } else {
      char timeBuf[12] = "";
      halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
      char dayBuf[32] = "";
      TimeUtils::formatDayOrdinal(TimeUtils::getLocalDayOrdinal(MIDAD_APP_SETTINGS.prayerLastSyncEpoch), dayBuf,
                                  sizeof(dayBuf));
      const bool syncedToday = TimeUtils::getLocalDayOrdinal(MIDAD_APP_SETTINGS.prayerLastSyncEpoch) == todayOrdinal;
      snprintf(syncValue, sizeof(syncValue), tr(STR_PRAYER_LAST_SYNCED), syncedToday ? timeBuf : dayBuf);
    }
  }
  drawRow(y, tr(STR_PRAYER_SYNC_NOW), syncValue, selectedRow == ROW_SYNC, false);

  // Footer: what to do next while the location is incomplete, the source once
  // it is set up. Same font as the rows.
  const char* info = tr(STR_PRAYER_SOURCE);
  if (!hasCountry()) {
    info = tr(STR_PRAYER_HINT_COUNTRY);
  } else if (MIDAD_APP_SETTINGS.prayerHasCity == 0) {
    info = tr(STR_PRAYER_HINT_CITY);
  } else if (!hasDay) {
    info = tr(STR_PRAYER_HINT_SYNC);
  }
  const int infoY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - renderer.getLineHeight(font);
  const int infoX = rtl ? pageWidth - sidePadding - renderer.getTextWidth(font, info) : sidePadding;
  renderer.drawText(font, infoX, infoY, info);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN), false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
