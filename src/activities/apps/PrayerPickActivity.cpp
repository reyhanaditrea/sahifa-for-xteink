#include "PrayerPickActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "MappedInputManager.h"
#include "MidadAppSettings.h"
#include "components/UITheme.h"
#include "prayer/PrayerCityTable.h"
#include "prayer/PrayerMethods.h"

namespace {
// U+25CF BLACK CIRCLE. Spelled as bytes, the way the rest of the codebase
// spells its non-ASCII punctuation (DictionaryStore.cpp) -- the literal that
// used to sit here had been through one encoding pass too many and drew as
// mojibake, so the method list marked its current row with three stray glyphs.
constexpr const char* SELECTED_MARK = "\xE2\x97\x8F";
}  // namespace

void PrayerPickActivity::onEnter() {
  Activity::onEnter();
  // Open on the current choice rather than the top of a 244-row list.
  selectedIndex = 0;
  if (mode == Mode::Method) {
    selectedIndex = MIDAD_APP_SETTINGS.prayerMethod;
  } else if (mode == Mode::AsrSchool) {
    selectedIndex = MIDAD_APP_SETTINGS.prayerAsrSchool;
  } else if (mode == Mode::Country) {
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
  if (mode == Mode::Method) return PrayerMethods::count();
  if (mode == Mode::AsrSchool) return PrayerMethods::asrSchoolCount();
  if (mode == Mode::Country) return PrayerCityTable::countryCount();
  return PrayerCityTable::cityEnd(countryIndex) - PrayerCityTable::cityBegin(countryIndex);
}

void PrayerPickActivity::activate() {
  auto& settings = MIDAD_APP_SETTINGS;
  if (mode == Mode::Method || mode == Mode::AsrSchool) {
    uint8_t& field = mode == Mode::Method ? settings.prayerMethod : settings.prayerAsrSchool;
    field = static_cast<uint8_t>(selectedIndex);
    settings.saveToFile();
    finish();
    return;
  }
  if (mode == Mode::Country) {
    const uint16_t picked = static_cast<uint16_t>(selectedIndex);
    const char* iso = picked == PrayerCityTable::CUSTOM_COUNTRY ? MidadAppSettings::PRAYER_CUSTOM_ISO
                                                                : PrayerCityTable::country(picked).iso;
    if (strcmp(settings.prayerCountryIso, iso) != 0) {
      // A different country invalidates the city chosen inside the old one.
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
  strlcpy(settings.prayerCityName, city.name, sizeof(settings.prayerCityName));
  settings.prayerLatE2 = city.latE2;
  settings.prayerLngE2 = city.lngE2;
  settings.prayerHasCity = 1;
  settings.saveToFile();
  finish();
}

bool PrayerPickActivity::handleArmedButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) confirmArmed = true;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backArmed = true;
  if (confirmArmed && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmArmed = false;
    activate();
    return true;
  }
  if (backArmed && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backArmed = false;
    finish();
    return true;
  }
  return false;
}

void PrayerPickActivity::loop() {
  if (handleArmedButtons()) return;

  const int count = itemCount();
  // Bind the physical pairs, not NavNext/ScrollNext: those two resolve to the
  // SAME buttons (side Down plus front Right, see MappedInputManager::
  // mapButton), so binding one to stepping and the other to paging fired both
  // on every press and only the page jump was visible.
  // ScreenLeft/ScreenRight are the front pair with the orientation transform
  // the hint labels already use; Up/Down are the side buttons, which are fixed.
  buttonNavigator.onRelease({MappedInputManager::Button::ScreenRight}, [this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onRelease({MappedInputManager::Button::ScreenLeft}, [this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, count] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, count, itemsPerPage);
    requestUpdate();
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, count] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, count, itemsPerPage);
    requestUpdate();
  });
}

void PrayerPickActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const char* title = mode == Mode::Method      ? tr(STR_PRAYER_METHOD)
                      : mode == Mode::AsrSchool ? tr(STR_PRAYER_ASR_SCHOOL)
                      : mode == Mode::Country   ? tr(STR_PRAYER_COUNTRY)
                                                : PrayerCityTable::country(countryIndex).name;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  itemsPerPage = GUI.getListPageItems(listHeight, false);

  const uint16_t cityBase = mode == Mode::City ? PrayerCityTable::cityBegin(countryIndex) : 0;

  // Country rows carry their ISO code as the value: two countries can share a
  // prefix in a long list, and the code is how the auto method is chosen. The
  // two enum lists mark the row in force instead -- neither has a second thing
  // worth saying about a row, and without a mark a list of conventions gives no
  // clue which one the times came from.
  std::function<std::string(int)> rowValue;
  switch (mode) {
    case Mode::Country:
      rowValue = [](const int index) -> std::string {
        return PrayerCityTable::country(static_cast<uint16_t>(index)).iso;
      };
      break;
    case Mode::Method:
      rowValue = [](const int index) -> std::string {
        return index == MIDAD_APP_SETTINGS.prayerMethod ? SELECTED_MARK : "";
      };
      break;
    case Mode::AsrSchool:
      rowValue = [](const int index) -> std::string {
        return index == MIDAD_APP_SETTINGS.prayerAsrSchool ? SELECTED_MARK : "";
      };
      break;
    case Mode::City:
      break;
  }

  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, itemCount(), selectedIndex,
      [this, cityBase](const int index) -> std::string {
        if (mode == Mode::Method) return I18N.get(PrayerMethods::label(static_cast<uint8_t>(index)));
        if (mode == Mode::AsrSchool) {
          return I18N.get(PrayerMethods::asrSchoolLabels()[static_cast<size_t>(index)]);
        }
        if (mode == Mode::Country) return PrayerCityTable::country(static_cast<uint16_t>(index)).name;
        return PrayerCityTable::city(static_cast<uint16_t>(cityBase + index)).name;
      },
      nullptr, nullptr, rowValue);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // The side buttons page a list that runs to hundreds of rows; nothing else
  // on screen would say so.
  if (itemCount() > itemsPerPage) {
    GUI.drawSideButtonHints(renderer, tr(STR_PAGE_UP), tr(STR_PAGE_DOWN));
  }
  renderer.displayBuffer();
}
