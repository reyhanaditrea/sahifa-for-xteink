#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "news/NewsIndex.h"

// Reads one item's text out of the feed's body file and pages it.
//
// What a feed gives is a one- to three-sentence summary, so the last page
// offers to fetch the article's own page. That costs Wi-Fi and a restart, and
// some sites refuse the device outright, so the offer disappears once the feed
// is known to be unreadable.
class NewsArticleActivity final : public Activity {
 public:
  NewsArticleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, size_t slot, uint16_t itemIndex,
                      std::string feedName);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  const char* activityDebugName() const override { return "NewsArticle"; }

 private:
  // What the last page offers, if anything.
  enum class Action : uint8_t {
    None,     // no link, or the text is already here
    Fetch,    // "Get full text"
    Queued,   // already waiting for the next sync
    Blocked,  // the feed's site does not give it up
  };

  bool loadRecord();
  void paginate();
  Action action() const { return action_; }
  int actionRowHeight() const;
  // One line of article text, anchored to the margin the script reads from.
  void drawAligned(int fontId, int y, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void openActionMenu();
  void queueForSync(bool queued);

  size_t slot_;
  uint16_t itemIndex_;
  std::string feedName_;

  NewsIndex::Record record_ = {};
  bool loaded_ = false;
  // The article's own text when it has been fetched, otherwise the feed's
  // summary. One or the other -- the article text opens with the summary.
  std::string body_;
  Action action_ = Action::None;
  OptionPopup actionPopup_;
  // Arabic article: title and body hug the right margin; everything else hugs
  // the left.
  bool rtlBody_ = false;

  std::vector<std::string> lines_;
  // pageStarts_[p] is the first line of page p; the last entry is the line count.
  std::vector<int> pageStarts_;
  int currentPage_ = 0;
};
