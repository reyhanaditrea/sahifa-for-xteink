#include "PrayerPickActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "MappedInputManager.h"
#include "MidadAppSettings.h"
#include "components/UITheme.h"
#include "prayer/PrayerCityTable.h"

void PrayerPickActivity::onEnter() {
  Activity::onEnter();
  // Open on the current choice rather than the top of a 244-row list.
  selectedIndex = 0;
  if (mode == Mode::Country) {
    const int current = PrayerCityTable::findCountry(MIDAD_APP_SETTINGS.prayerCountryIso);
    if (current > 0) selectedIndex = current;
  } else if (MIDAD_APP_SETTINGS.prayerHasCity != 0) {
    const uint16_t begin = PrayerCityTable::cityBegin(countryIndex);
    const uint16_t end = PrayerCityTable::cityEnd(countryIndex);
    for (uint16_t i = begin; i < end; ++i) {
      if (strcmp(PrayerCityTable::city(i).name, MIDAD_APP_SETTINGS.prayerCityName) == 0) {
        selectedIndex = static_cast<int>(i - begin);
        break;
      }
    }
  }
  requestUpdate();
}

int PrayerPickActivity::itemCount() const {
  if (mode == Mode::Country) return PrayerCityTable::countryCount();
  return PrayerCityTable::cityEnd(countryIndex) - PrayerCityTable::cityBegin(countryIndex);
}

void PrayerPickActivity::activate() {
  auto& settings = MIDAD_APP_SETTINGS;
  if (mode == Mode::Country) {
    const uint16_t picked = static_cast<uint16_t>(selectedIndex);
    const char* iso = picked == PrayerCityTable::CUSTOM_COUNTRY ? MidadAppSettings::PRAYER_CUSTOM_ISO
                                                                : PrayerCityTable::country(picked).iso;
    if (strcmp(settings.prayerCountryIso, iso) != 0) {
      // A different country invalidates the city that was chosen inside the
      // old one, and with it every stored month.
      strlcpy(settings.prayerCountryIso, iso, sizeof(settings.prayerCountryIso));
      settings.prayerCityName[0] = '\0';
      settings.prayerHasCity = 0;
      settings.prayerLatE2 = 0;
      settings.prayerLngE2 = 0;
      settings.saveToFile();
    }
    finish();
    return;
  }

  const PrayerCity& city = PrayerCityTable::city(
      static_cast<uint16_t>(PrayerCityTable::cityBegin(countryIndex) + static_cast<uint16_t>(selectedIndex)));
  const bool changed = settings.prayerLatE2 != city.latE2 || settings.prayerLngE2 != city.lngE2;
  strlcpy(settings.prayerCityName, city.name, sizeof(settings.prayerCityName));
  settings.prayerLatE2 = city.latE2;
  settings.prayerLngE2 = city.lngE2;
  settings.prayerHasCity = 1;
  settings.saveToFile();
  finish();
}

void PrayerPickActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate();
    return;
  }

  const int count = itemCount();
  buttonNavigator.onScrollNextRelease([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onScrollPreviousRelease([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
  // A country list is 244 rows long: page jumps make it usable on four buttons.
  buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this, count] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, count, itemsPerPage);
    requestUpdate();
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this, count] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, count, itemsPerPage);
    requestUpdate();
  });
}

void PrayerPickActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const char* title = mode == Mode::Country ? tr(STR_PRAYER_COUNTRY) : PrayerCityTable::country(countryIndex).name;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  itemsPerPage = GUI.getListPageItems(listHeight, false);

  const uint16_t cityBase = mode == Mode::City ? PrayerCityTable::cityBegin(countryIndex) : 0;
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, itemCount(), selectedIndex,
      [this, cityBase](const int index) -> std::string {
        if (mode == Mode::Country) return PrayerCityTable::country(static_cast<uint16_t>(index)).name;
        return PrayerCityTable::city(static_cast<uint16_t>(cityBase + index)).name;
      },
      nullptr, nullptr,
      // Country rows carry their ISO code as the value: two countries can share
      // a prefix in a long list, and the code is how the auto method is chosen.
      mode == Mode::Country ? std::function<std::string(int)>([](const int index) -> std::string {
        return PrayerCityTable::country(static_cast<uint16_t>(index)).iso;
      })
                            : nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN), false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
