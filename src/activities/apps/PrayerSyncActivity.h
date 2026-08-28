#pragma once

#include <I18n.h>

#include <cstdint>

#include "activities/Activity.h"

// Fetches this month's prayer times from the Aladhan API for the location in
// MidadAppSettings, plus next month when the current one is nearly over, then
// deletes the months that have already passed. Same shape as
// GymCatalogSyncActivity: pick a network, download to a temp file, parse into
// the compact month file, tear WiFi down and silent-restart back into the app
// rather than continue on a TLS-fragmented heap.
class PrayerSyncActivity final : public Activity {
 public:
  PrayerSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == SYNCING; }
  bool skipLoopDelay() override { return true; }

 private:
  enum State : uint8_t {
    WIFI_SELECTION,
    SYNCING,
    COMPLETE,
    ERROR,
  };

  void onWifiSelectionComplete(bool success);
  void sync();
  // Downloads and imports one month; leaves no temp file behind.
  bool syncMonth(int year, unsigned month);

  State state_ = WIFI_SELECTION;
  StrId errorMessage_ = StrId::STR_PRAYER_SYNC_FAILED;
  int monthsStored_ = 0;
};
