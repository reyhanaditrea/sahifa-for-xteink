#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "components/UiAppHelpers.h"

// Manages which feeds the sync downloads.
//
// Two kinds of row, deliberately different: the built-in outlets ship with the
// firmware and cannot be removed, while feeds the user added can. That is why
// the built-ins are not stored as ordinary entries -- a later firmware can add
// an outlet and have it appear for everyone.
//
// Confirm on either kind opens a menu rather than acting: with enable/disable,
// move up and move down all wanted on the same row, one button cannot carry
// them, and a menu titled with the feed's own name says which row is about to
// change.
class NewsFeedsActivity final : public UiListActivity {
 public:
  NewsFeedsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;
  const char* activityDebugName() const override { return "NewsFeeds"; }

 protected:
  int listCount() const override { return static_cast<int>(rows_.size()); }
  bool handleCustomInput() override;
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
  // The per-feed menu: enable/disable, move up, move down, and remove on a
  // user feed. Built entry by entry, because which entries apply depends on
  // where the row sits in its section.
  void showFeedMenu(int index);
  // Flips one feed's enabled state and persists it.
  void toggleRow(const Row& row);
  // Where a row sits in the list right now; -1 once it is gone.
  int rowIndexOf(const Row& row) const;
  // Moves the row one place within its own section, carrying its downloaded
  // articles with it.
  void moveRow(int index, int delta);
  // Clears the downloaded cache: feed slots shift when the user list changes,
  // so a stale index would point at another feed's articles.
  static void invalidateCache();
  // Swaps two slots' cached feedN.idx/.txt. A move renumbers the slots, and
  // re-downloading is a Wi-Fi round trip -- for an article whose full text was
  // fetched, a round trip plus a reboot -- so the cache follows the feed
  // instead of being thrown away. False if a rename failed partway, which
  // leaves the cache mismatched and the caller clearing it.
  static bool swapCachedFeeds(size_t slotA, size_t slotB);
  // The sync slot a row occupies: built-ins hold the first slots, in the order
  // shown, then the user feeds.
  size_t slotForRow(const Row& row) const;

  std::vector<Row> rows_;
  // Backing storage for the row labels, which the list borrows as const char*.
  std::vector<std::string> labels_;
  std::vector<std::string> subtitles_;
  std::string addFeedValue_;
  std::vector<freeink::ui::ListItem> items_;
  OptionPopup menuPopup_;
};
