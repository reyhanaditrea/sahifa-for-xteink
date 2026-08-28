#include "PrayerSyncActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <ctime>

#include "MappedInputManager.h"
#include "MidadAppSettings.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "prayer/PrayerMethods.h"
#include "prayer/PrayerPure.h"
#include "prayer/PrayerStore.h"
#include "util/TimeUtils.h"

PrayerSyncActivity::PrayerSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("PrayerSync", renderer, mappedInput) {}

void PrayerSyncActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void PrayerSyncActivity::onExit() {
  Activity::onExit();
  // Same reasoning as the Gym catalog sync: don't run the rest of the session
  // on the heap a TLS download fragmented. Targeted so the user lands back on
  // the prayer screen instead of Home.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToPrayer();
  }
}

void PrayerSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }
  {
    RenderLock lock(*this);
    state_ = SYNCING;
  }
  requestUpdateAndWait();
  sync();
}

bool PrayerSyncActivity::syncMonth(const int year, const unsigned month) {
  char url[192];
  PrayerPure::buildCalendarUrl(url, sizeof(url), year, month, MIDAD_APP_SETTINGS.prayerLatE2,
                               MIDAD_APP_SETTINGS.prayerLngE2, PrayerMethods::effectiveAladhanId());

  Storage.mkdir(PrayerStore::DIR);
  Storage.remove(PrayerStore::TMP_PATH);
  const auto result = HttpDownloader::downloadToFile(url, PrayerStore::TMP_PATH, nullptr);
  if (result != HttpDownloader::OK) {
    LOG_ERR("PRAY", "fetch %04d-%02u failed (%d)", year, month, static_cast<int>(result));
    Storage.remove(PrayerStore::TMP_PATH);
    return false;
  }

  const bool imported = PrayerStore::importAladhan(PrayerStore::TMP_PATH, year, month, MIDAD_APP_SETTINGS.prayerLatE2,
                                                   MIDAD_APP_SETTINGS.prayerLngE2, PrayerMethods::effectiveAladhanId());
  Storage.remove(PrayerStore::TMP_PATH);
  return imported;
}

void PrayerSyncActivity::sync() {
  // WifiSelectionActivity already quick-syncs the system clock on connect, but
  // this flow cannot proceed without a date at all (it picks which month to
  // fetch), so make the dependency explicit.
#ifndef SIMULATOR
  if (!HalClock::isSystemTimeValid()) HalClock::quickSyncSystemTime();
#endif
  const uint32_t today = TimeUtils::todayOrdinal();
  int year;
  unsigned month, day;
  if (today == 0 || !TimeUtils::getDateFromDayOrdinal(today, year, month, day)) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = StrId::STR_PRAYER_CLOCK_INVALID;
    return;
  }

  if (!syncMonth(year, month)) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = StrId::STR_PRAYER_SYNC_FAILED;
    return;
  }
  monthsStored_ = 1;

  // Near the end of the month, fetch the next one too: the following morning
  // would otherwise show no times with no network in reach.
  if (PrayerStore::needsNextMonth(today)) {
    const int nextYear = month == 12 ? year + 1 : year;
    const unsigned nextMonth = month == 12 ? 1 : month + 1;
    // A failure here is not fatal -- this month is already stored.
    if (syncMonth(nextYear, nextMonth)) monthsStored_ = 2;
  }

  PrayerStore::pruneBefore(year, month);
  MIDAD_APP_SETTINGS.prayerLastSyncEpoch = static_cast<uint32_t>(time(nullptr));
  MIDAD_APP_SETTINGS.saveToFile();

  RenderLock lock(*this);
  state_ = COMPLETE;
}

void PrayerSyncActivity::loop() {
  if (state_ == COMPLETE || state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}

void PrayerSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PRAYER_SYNC_NOW));

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int centreY = (pageHeight - lineHeight) / 2;

  if (state_ == SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, centreY, tr(STR_LOADING));
  } else if (state_ == COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, centreY - lineHeight, tr(STR_PRAYER_SYNC_OK), true, EpdFontFamily::BOLD);
    char buf[64];
    snprintf(buf, sizeof(buf), tr(STR_PRAYER_MONTHS_STORED), monthsStored_);
    renderer.drawCenteredText(UI_10_FONT_ID, centreY + metrics.verticalSpacing, buf);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centreY - lineHeight, tr(STR_DOWNLOAD_FAILED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centreY + metrics.verticalSpacing, I18N.get(errorMessage_));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
