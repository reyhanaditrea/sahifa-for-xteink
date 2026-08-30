#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "TbGame.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Textbound: a text roguelike. Every screen is narrative plus a short list, so
// this class only ever asks tb::Game for rows and draws them -- all rules,
// generation and serialization live in the engine, which the host tests drive
// directly.
class TextboundActivity final : public Activity {
 public:
  explicit TextboundActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Textbound", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;
  const char* activityDebugName() const override { return "Textbound"; }

 private:
  enum class Mode : uint8_t { Title, Playing, Paused };
  enum TitleRow : uint8_t { TITLE_NEW = 0, TITLE_CONTINUE };
  // The run is written to the card after every action, so "save and exit" only
  // has to leave; it is spelled out because "back out and it is gone" is what
  // players assume otherwise.
  enum PauseRow : uint8_t { PAUSE_RESUME = 0, PAUSE_SAVE_EXIT, PAUSE_ABANDON, PAUSE_ROW_COUNT };

  void startNewRun();
  void continueRun();
  void activateRow();
  void openPause();
  void saveAndExit();
  void abandonRun();
  void afterAction();  // repaginate, clamp the cursor, persist, redraw
  void paginate();
  void persist();
  void recordFinishedRun();

  int listRowCount() const;
  std::string listRowText(int index) const;
  bool listRowDimmed(int index) const;
  int pageCount() const;

  std::unique_ptr<tb::Game> game_;
  ButtonNavigator buttonNavigator_;

  Mode mode_ = Mode::Title;
  int selected_ = 0;
  int pauseSelected_ = 0;
  int currentPage_ = 0;
  bool runRecorded_ = false;
  // Back is often still held by the menu that launched this screen; its release
  // would otherwise pop straight back out.
  bool backHandled_ = false;

  std::vector<std::string> lines_;
  // pageStarts_[p] is the first line of page p; the last entry is the count.
  std::vector<int> pageStarts_;
};
