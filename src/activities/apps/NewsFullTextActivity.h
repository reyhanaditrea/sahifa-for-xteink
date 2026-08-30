#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "news/NewsIndex.h"

// Fetches one article's own page over Wi-Fi and stores its text next to the
// feed's summary.
//
// Shaped like NewsAddFeedActivity: pick a network, do the work while the screen
// shows progress, drop Wi-Fi and silently restart into News. The article being
// read is remembered in the store first, so the restart lands back on it rather
// than on the headline list.
class NewsFullTextActivity final : public Activity {
 public:
  NewsFullTextActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, size_t slot, uint16_t itemIndex,
                       std::string feedName);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == FETCHING; }
  bool skipLoopDelay() override { return true; }
  const char* activityDebugName() const override { return "NewsFullText"; }

  // Reads the record, fetches its URL and writes the text back into the slot's
  // files. Shared with the sync, which does the same for queued items.
  static bool fetchInto(size_t slot, uint16_t itemIndex);

 private:
  enum State : uint8_t { WIFI_SELECTION, FETCHING, COMPLETE, ERROR };

  void onWifiSelectionComplete(bool connected);
  void fetch();

  size_t slot_;
  uint16_t itemIndex_;
  std::string feedName_;
  State state_ = WIFI_SELECTION;
};
