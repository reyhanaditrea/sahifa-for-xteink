#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Resolves what the user typed into a feed URL and stores it.
//
// The keyboard runs before this activity (in NewsFeedsActivity) so the typing
// happens without Wi-Fi powered up; this screen only picks a network, runs
// discovery, and reports the result.
class NewsAddFeedActivity final : public Activity {
 public:
  NewsAddFeedActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string input);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == DISCOVERING; }
  bool skipLoopDelay() override { return true; }
  const char* activityDebugName() const override { return "NewsAddFeed"; }

 private:
  enum State : uint8_t { WIFI_SELECTION, DISCOVERING, COMPLETE, ERROR };

  void onWifiSelectionComplete(bool connected);
  void discover();

  std::string input_;
  std::string resolvedUrl_;
  const char* errorMessage_ = nullptr;
  State state_ = WIFI_SELECTION;
};
