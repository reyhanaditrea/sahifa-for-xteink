#include "PrayerCoordsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "MidadAppSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "prayer/PrayerPure.h"
#include "prayer/PrayerStore.h"

namespace {
constexpr int32_t LAT_LIMIT = 9000;
constexpr int32_t LNG_LIMIT = 18000;

int32_t clamp(const int32_t value, const int32_t limit) {
  if (value > limit) return limit;
  if (value < -limit) return -limit;
  return value;
}
}  // namespace

void PrayerCoordsActivity::onEnter() {
  Activity::onEnter();
  latE2 = clamp(MIDAD_APP_SETTINGS.prayerLatE2, LAT_LIMIT);
  lngE2 = clamp(MIDAD_APP_SETTINGS.prayerLngE2, LNG_LIMIT);
  activeField = FIELD_LAT_DEGREES;
  requestUpdate();
}

void PrayerCoordsActivity::onExit() {
  auto& settings = MIDAD_APP_SETTINGS;
  const bool changed = settings.prayerLatE2 != latE2 || settings.prayerLngE2 != lngE2;
  if (changed || settings.prayerHasCity == 0) {
    settings.prayerLatE2 = latE2;
    settings.prayerLngE2 = lngE2;
    settings.prayerHasCity = 1;
    settings.prayerCityName[0] = '\0';  // the row renders the coordinates instead
    settings.saveToFile();
    if (changed) PrayerStore::clearMonths();
  }
  Activity::onExit();
}

void PrayerCoordsActivity::adjust(const int delta) {
  switch (activeField) {
    case FIELD_LAT_DEGREES:
      latE2 = clamp(latE2 + delta * 100, LAT_LIMIT);
      break;
    case FIELD_LAT_FRACTION:
      latE2 = clamp(latE2 + delta, LAT_LIMIT);
      break;
    case FIELD_LNG_DEGREES:
      lngE2 = clamp(lngE2 + delta * 100, LNG_LIMIT);
      break;
    case FIELD_LNG_FRACTION:
    default:
      lngE2 = clamp(lngE2 + delta, LNG_LIMIT);
      break;
  }
  requestUpdate();
}

void PrayerCoordsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activeField = static_cast<Field>((activeField + 1) % FIELD_COUNT);
    requestUpdate();
    return;
  }
  // Continuous, not release-only: crossing tens of degrees a press at a time
  // would be unusable.
  buttonNavigator.onScrollNextContinuous([this] { adjust(-1); });
  buttonNavigator.onScrollPreviousContinuous([this] { adjust(1); });
}

void PrayerCoordsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PRAYER_CUSTOM_COORDS));

  const int font = UI_12_FONT_ID;
  const int lineHeight = renderer.getLineHeight(font);
  const int centreY = pageHeight / 2 - lineHeight;

  // Each value is drawn as two boxes -- degrees and hundredths -- so the
  // selected one can invert on its own.
  auto drawValue = [&](const int y, const StrId label, const int32_t valueE2, const bool isLatitude,
                       const Field degreesField, const Field fractionField) {
    char hemisphere[16];
    PrayerPure::formatCoordinate(hemisphere, sizeof(hemisphere), valueE2, isLatitude);
    const int32_t abs = valueE2 < 0 ? -valueE2 : valueE2;
    char degrees[8];
    char fraction[8];
    snprintf(degrees, sizeof(degrees), "%c %ld", hemisphere[0], static_cast<long>(abs / 100));
    snprintf(fraction, sizeof(fraction), ".%02ld", static_cast<long>(abs % 100));

    const int degreesWidth = renderer.getTextWidth(font, degrees, EpdFontFamily::BOLD);
    const int fractionWidth = renderer.getTextWidth(font, fraction, EpdFontFamily::BOLD);
    const char* labelText = I18N.get(label);
    const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, labelText);
    constexpr int padding = 6;
    const int totalWidth = degreesWidth + fractionWidth + padding * 4;
    int x = (pageWidth - totalWidth) / 2;

    renderer.drawText(UI_10_FONT_ID, (pageWidth - labelWidth) / 2, y - lineHeight, labelText);
    const bool degreesSelected = activeField == degreesField;
    const bool fractionSelected = activeField == fractionField;
    if (degreesSelected) renderer.fillRect(x, y - 2, degreesWidth + padding * 2, lineHeight + 4);
    renderer.drawText(font, x + padding, y, degrees, !degreesSelected, EpdFontFamily::BOLD);
    x += degreesWidth + padding * 2;
    if (fractionSelected) renderer.fillRect(x, y - 2, fractionWidth + padding * 2, lineHeight + 4);
    renderer.drawText(font, x + padding, y, fraction, !fractionSelected, EpdFontFamily::BOLD);
  };

  drawValue(centreY, StrId::STR_PRAYER_LATITUDE, latE2, true, FIELD_LAT_DEGREES, FIELD_LAT_FRACTION);
  drawValue(centreY + lineHeight * 3, StrId::STR_PRAYER_LONGITUDE, lngE2, false, FIELD_LNG_DEGREES, FIELD_LNG_FRACTION);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEXT_FIELD), tr(STR_DIR_UP), tr(STR_DIR_DOWN), false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
