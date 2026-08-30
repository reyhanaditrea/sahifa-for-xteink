#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/UiAppHelpers.h"
#include "news/NewsIndex.h"

// The News reading screen.
//
// Layout, top to bottom: a "Sync news" row, a "Feeds" row, a rule, then a tab
// bar for the three days the app keeps, then that day's headlines grouped under
// their feed. Front Up/Down walk the rows (headers are skipped); the side
// buttons page the headline list, which is what makes a long day readable
// without a hundred clicks.
//
// Tabs follow the real clock rather than the sync, so crossing midnight leaves
// "Today" empty with a hint instead of quietly relabelling yesterday's news.
class NewsActivity final : public UiListActivity {
 public:
  NewsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  const char* activityDebugName() const override { return "News"; }

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
  void drawFooter() override;
  void navigateButtons() override;
  bool handleCustomInput() override;

 private:
  // Fixed rows above the tab bar, then the tab row itself.
  static constexpr int ROW_SYNC = 0;
  static constexpr int ROW_FEEDS = 1;
  static constexpr int ROW_TABS = 2;
  static constexpr int FIXED_ROWS = 3;

  // One cached item: enough to find it again on SD without holding its title.
  struct Entry {
    uint8_t slot;
    uint16_t item;
    uint8_t day;  // 0 today, 1 yesterday, 2 the day before
    uint32_t epoch;
  };
  // A row of the day list: either a feed header or one of that feed's items.
  struct DayRow {
    bool header;
    uint8_t slot;
    uint16_t entry;  // index into entries_ for item rows
  };

  void loadIndexes();
  void rebuildDayRows();
  void paginate();
  // Height of a feed-header row, shared by the layout and the pagination.
  int headerRowHeight() const;
  int sectionGapHeight() const;
  void loadPageLabels();
  void refreshTopRows();
  int pageCount() const;
  int rowsOnPage() const;
  // Maps a cursor position to the entry it points at; -1 for non-item rows.
  int entryForCursor(int cursor) const;
  // Puts the cursor back on one item: picks its day tab, its page, and its row.
  // False when the item is no longer in the cache.
  bool focusEntry(uint8_t slot, uint16_t item);
  // Next row the cursor may rest on, skipping feed headers.
  int nextSelectable(int from, int direction) const;
  void changeTab(int delta);
  void changePage(int delta);

  std::vector<Entry> entries_;
  std::vector<DayRow> dayRows_;
  std::vector<int> pageStarts_;

  // Labels for the rows currently on screen; the list borrows these.
  std::vector<std::string> pageLabels_;
  std::vector<std::string> pageValues_;
  std::vector<freeink::ui::ListItem> pageItems_;
  std::vector<freeink::ui::ListItem> topItems_;
  std::string syncValue_;
  std::string feedsValue_;
  std::string tabLabels_[NewsIndex::DAY_COUNT];
  const char* topLabels_[2] = {nullptr, nullptr};

  uint32_t todayOrdinal_ = 0;
  int32_t localOffsetSec_ = 0;
  int tab_ = 0;
  int page_ = 0;
  bool synced_ = false;
};
