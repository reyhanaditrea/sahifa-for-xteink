#pragma once

#include "SilentRestart.h"
#include "activities/Activity.h"

// Manual NTP resync action. Runs a forced sync (bypassing the once-per-device debounce),
// reports success/failure, then waits for Back. If WiFi is not connected yet, it reuses the
// normal WiFi selection flow first.
class ClockSyncActivity final : public Activity {
 public:
  // restartOnExit runs when this screen brought WiFi up itself and has to
  // clear the fragmented heap on the way out. It decides where that reboot
  // lands: Home by default, or back at whichever screen sent the user here
  // (see the silentRestartTo* family). A plain function pointer, because the
  // callback outlives this activity -- it is the last thing it does.
  explicit ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             void (*restartOnExit)() = &silentRestart)
      : Activity("ClockSync", renderer, mappedInput), restartOnExit(restartOnExit) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, SUCCESS, NO_WIFI, FAILED };
  State state = SYNCING;
  char syncedTime[16] = {0};
  bool shouldTearDownWifiOnExit = false;

  void (*const restartOnExit)();

  void runSync();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
};
