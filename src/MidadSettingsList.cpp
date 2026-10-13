#include "MidadSettingsList.h"

#include "MidadAppSettings.h"
#include "SettingsList.h"

void appendMidadAppSettings(std::vector<SettingInfo>& appsSettings) {
  // Quran toggle: SettingsActivity::toggleCurrentSetting() extracts the
  // firmware-embedded EPUB to SD when this turns on (QuranBook::ensureExtracted).
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_QURAN, [] { return MIDAD_APP_SETTINGS.quranEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.quranEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "quranEnabled", StrId::STR_CAT_APPS));
  // Prayer Times toggle only. The calculation method and the Asr school are on
  // the app's own screen next to the times they change -- see PrayerActivity's
  // Row enum. Both are still written to settings.json under the same keys, so
  // the Midad app keeps syncing them.
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_PRAYER_TIMES, [] { return MIDAD_APP_SETTINGS.prayerEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.prayerEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "prayerEnabled", StrId::STR_CAT_APPS));
  // Learn to Read toggle: the flashcard app for small children.
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_KIDS_READ, [] { return MIDAD_APP_SETTINGS.kidsReadEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.kidsReadEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "kidsReadEnabled", StrId::STR_CAT_APPS));
  // Games toggle: pins a "Games" tile in My Books that opens a Snake/Tetris
  // picker. No extraction step needed -- unlike Quran, nothing but the
  // toggle itself is required.
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_GAMES, [] { return MIDAD_APP_SETTINGS.gamesEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.gamesEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "gamesEnabled", StrId::STR_CAT_APPS));
  // Tasbih toggle: pins a "Tasbih" tile in My Books that opens the built-in
  // dhikr counter. No extraction step needed, same as Games.
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_TASBIH, [] { return MIDAD_APP_SETTINGS.tasbihEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.tasbihEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "tasbihEnabled", StrId::STR_CAT_APPS));
  // News toggle: shows the News tile, which reads RSS/Atom feeds downloaded by the
  // device itself. The field keeps its `rssEnabled` name and JSON key so existing
  // settings.json files and the web sync keep working.
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_NEWS, [] { return MIDAD_APP_SETTINGS.rssEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.rssEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "rssEnabled", StrId::STR_CAT_APPS));
  // Stop Watch toggle: pins a "Stop Watch" tile in My Books that opens the
  // built-in stopwatch. No extraction step needed, same as Games.
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_STOPWATCH, [] { return MIDAD_APP_SETTINGS.stopwatchEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.stopwatchEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "stopwatchEnabled", StrId::STR_CAT_APPS));
  // Pomodoro toggle: pins a "Pomodoro" tile in My Books opening
  // StopwatchActivity in Pomodoro mode. The three durations below sit
  // directly under it so the group reads as one feature. Ranges cover the
  // common variants -- 25/5/15 classic, 52/17, 90-minute deep work -- and
  // start above 0, since a zero-length phase would expire instantly.
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_POMODORO, [] { return MIDAD_APP_SETTINGS.pomodoroEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.pomodoroEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "pomodoroEnabled", StrId::STR_CAT_APPS));
  // The three durations describe a Pomodoro cycle that cannot run while the app
  // itself is off, so they stay hidden until it is switched on.
  const auto pomodoroOn = [] { return MIDAD_APP_SETTINGS.pomodoroEnabled != 0; };
  appsSettings.push_back(SettingInfo::DynamicValue(
                             StrId::STR_POMODORO_FOCUS_MIN, [] { return MIDAD_APP_SETTINGS.pomodoroFocusMin; },
                             [](uint8_t val) {
                               MIDAD_APP_SETTINGS.pomodoroFocusMin = val;
                               MIDAD_APP_SETTINGS.saveToFile();
                             },
                             {5, 90, 5}, "pomodoroFocusMin", StrId::STR_CAT_APPS)
                             .shownWhen(pomodoroOn));
  appsSettings.push_back(SettingInfo::DynamicValue(
                             StrId::STR_POMODORO_SHORT_BREAK_MIN,
                             [] { return MIDAD_APP_SETTINGS.pomodoroShortBreakMin; },
                             [](uint8_t val) {
                               MIDAD_APP_SETTINGS.pomodoroShortBreakMin = val;
                               MIDAD_APP_SETTINGS.saveToFile();
                             },
                             {1, 30, 1}, "pomodoroShortBreakMin", StrId::STR_CAT_APPS)
                             .shownWhen(pomodoroOn));
  appsSettings.push_back(SettingInfo::DynamicValue(
                             StrId::STR_POMODORO_LONG_BREAK_MIN, [] { return MIDAD_APP_SETTINGS.pomodoroLongBreakMin; },
                             [](uint8_t val) {
                               MIDAD_APP_SETTINGS.pomodoroLongBreakMin = val;
                               MIDAD_APP_SETTINGS.saveToFile();
                             },
                             {5, 60, 5}, "pomodoroLongBreakMin", StrId::STR_CAT_APPS)
                             .shownWhen(pomodoroOn));
  // Gates whether the rolling SD diagnostic logs get written at all -- see
  // MidadAppSettings::debugLoggingEnabled and src/util/DebugLogging.h for the
  // full list and rationale. Last in this function by design (user request):
  // it should always land after KOReader Sync, which SettingsActivity appends
  // immediately before calling appendMidadAppSettings().
  appsSettings.push_back(SettingInfo::DynamicToggle(
      StrId::STR_DEBUG_LOGGING, [] { return MIDAD_APP_SETTINGS.debugLoggingEnabled; },
      [](uint8_t val) {
        MIDAD_APP_SETTINGS.debugLoggingEnabled = val;
        MIDAD_APP_SETTINGS.saveToFile();
      },
      "debugLoggingEnabled", StrId::STR_CAT_APPS));
}

std::vector<SettingInfo> getCombinedSettingsList(const SdCardFontRegistry* registry) {
  std::vector<SettingInfo> settings = getSettingsList(registry);
  appendMidadAppSettings(settings);
  return settings;
}
