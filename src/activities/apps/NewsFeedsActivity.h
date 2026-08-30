#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/UiAppHelpers.h"

// Manages which feeds the sync downloads.
//
// Two kinds of row, deliberately different: the built-in outlets ship with the
// firmware and only turn on and off, while feeds the user added can be removed.
// That is why the built-ins are not stored as ordinary entries -- a later
// firmware can add an outlet and have it appear for everyone.
class NewsFeedsActivity final : public UiListActivity {
 public:
  NewsFeedsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  const char* activityDebugName() const override { return "NewsFeeds"; }

 protected:
  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;
  void navigateButtons() override;

 private:
  enum class RowKind : uint8_t { AddFeed, SectionHeader, UserFeed, Builtin };

  struct Row {
    RowKind kind;
    size_t index;  // user-feed index or built-in index
  };

  // Next non-header row in `direction`, wrapping at both ends.
  int nextSelectable(int from, int direction) const;

  void rebuildRows();
  void promptAddFeed();
  void promptRemove(size_t userIndex);
  // Clears the downloaded cache: feed slots shift when the user list changes,
  // so a stale index would point at another feed's articles.
  static void invalidateCache();

  std::vector<Row> rows_;
  // Backing storage for the row labels, which the list borrows as const char*.
  std::vector<std::string> labels_;
  std::vector<std::string> subtitles_;
  std::string addFeedValue_;
  std::vector<freeink::ui::ListItem> items_;
};
