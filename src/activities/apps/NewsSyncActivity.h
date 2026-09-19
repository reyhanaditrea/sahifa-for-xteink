#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "news/NewsIndex.h"

// Downloads every enabled feed over Wi-Fi and rewrites the on-SD cache.
//
// Pick a network, do the work while the
// screen shows progress, then drop Wi-Fi and silently restart back into News so
// the reading screen starts from a fresh heap.
class NewsSyncActivity final : public Activity {
 public:
  NewsSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == SYNCING; }
  bool skipLoopDelay() override { return true; }
  const char* activityDebugName() const override { return "NewsSync"; }

 private:
  enum State : uint8_t { WIFI_SELECTION, SYNCING, COMPLETE, ERROR };

  void onWifiSelectionComplete(bool connected);
  void syncAll();
  // Downloads and parses one feed into its slot files. Returns items kept.
  int syncOneFeed(size_t slot);
  // Appends the items already on SD that are still inside the three-day window
  // and are not in the freshly parsed set. Returns the new total item count.
  uint16_t mergePreviousItems(size_t slot, const char* indexTmp, const char* bodyTmp, uint16_t freshCount);
  // Drops cache files belonging to slots that no longer exist, plus scratch
  // files from an interrupted run. Items themselves are kept and merged.
  static void pruneCache(size_t slotCount);
  // Fetches the article pages the reader flagged while offline. Runs after the
  // feeds, on the Wi-Fi connection the sync already holds.
  void fetchQueuedArticles(size_t slots);
  // Article pages are hundreds of kilobytes each; more than this per sync would
  // turn a thirty-second job into a several-minute one.
  static constexpr size_t MAX_QUEUED_FETCHES = 10;

  State state_ = WIFI_SELECTION;
  // Progress, read by render() under the render lock.
  int currentFeed_ = 0;
  int totalFeeds_ = 0;
  char currentFeedName_[32] = {};
  int itemsTotal_ = 0;
  int feedsOk_ = 0;
  // Queued full-text fetches, shown as their own progress line.
  int articlesDone_ = 0;
  int articlesTotal_ = 0;

  // The three local day ordinals we keep; all zero when the clock is unusable,
  // which makes the parser keep everything.
  uint32_t keepOrdinals_[NewsIndex::DAY_COUNT] = {};
  int32_t localOffsetSec_ = 0;
};
