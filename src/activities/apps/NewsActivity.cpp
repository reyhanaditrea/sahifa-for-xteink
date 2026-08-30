#include "activities/apps/NewsActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "NewsFeedStore.h"
#include "activities/apps/NewsArticleActivity.h"
#include "activities/apps/NewsFeedsActivity.h"
#include "activities/apps/NewsSyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "news/NewsDate.h"
#include "news/NewsPaths.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

namespace {

// Items with no usable date are filed under the day the sync ran, which is the
// only honest guess and keeps them visible instead of silently dropped.
constexpr uint8_t DAY_UNKNOWN_FALLBACK = 0;

void formatClock(const uint32_t epoch, const int32_t offsetSec, char* buf, const size_t len) {
  if (epoch == 0) {
    if (len > 0) buf[0] = '\0';
    return;
  }
  const int64_t local = static_cast<int64_t>(epoch) + offsetSec;
  const int secondsOfDay = static_cast<int>(((local % 86400) + 86400) % 86400);
  snprintf(buf, len, "%02d:%02d", secondsOfDay / 3600, (secondsOfDay % 3600) / 60);
}

}  // namespace

NewsActivity::NewsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("News", renderer, mappedInput) {}

void NewsActivity::onEnter() {
  UiListActivity::onEnter();
  {
    RenderLock lock(*this);
    localOffsetSec_ = TimeUtils::localOffsetSeconds();
    todayOrdinal_ = TimeUtils::todayOrdinal();
    loadIndexes();
    refreshTopRows();
    rebuildDayRows();
    nav.selected = ROW_SYNC;
  }

  // A full-text fetch ends in a silent restart, which lands here. Reopen the
  // article the user was reading rather than dropping them on the list.
  // Outside the lock: starting an activity takes it again, and it is not
  // recursive.
  size_t pendingSlot = 0;
  uint16_t pendingItem = 0;
  if (NEWS_FEEDS.pendingArticle(pendingSlot, pendingItem)) {
    NEWS_FEEDS.clearPendingArticle();
    if (pendingSlot < NEWS_FEEDS.slotCount()) {
      startActivityForResult(std::make_unique<NewsArticleActivity>(renderer, mappedInput, pendingSlot, pendingItem,
                                                                   NEWS_FEEDS.slotName(pendingSlot)),
                             [](const ActivityResult&) {});
    }
  }
}

void NewsActivity::loadIndexes() {
  entries_.clear();
  synced_ = false;

  const size_t slots = NEWS_FEEDS.slotCount();
  for (size_t slot = 0; slot < slots; slot++) {
    if (!NEWS_FEEDS.slotEnabled(slot)) continue;

    char indexPath[64];
    NewsPaths::indexPath(slot, indexPath, sizeof(indexPath));
    HalFile index;
    if (!Storage.openFileForRead("NEWS", indexPath, index)) continue;

    NewsIndex::Header header{};
    if (index.read(&header, sizeof(header)) != static_cast<int>(sizeof(header))) continue;
    if (header.magic != NewsIndex::MAGIC || header.version != NewsIndex::VERSION) continue;
    synced_ = true;

    entries_.reserve(entries_.size() + header.count);
    for (uint16_t i = 0; i < header.count; i++) {
      NewsIndex::Record record{};
      if (index.read(&record, sizeof(record)) != static_cast<int>(sizeof(record))) break;

      uint8_t day = DAY_UNKNOWN_FALLBACK;
      if (record.dayOrdinal != 0 && todayOrdinal_ != 0) {
        if (record.dayOrdinal > todayOrdinal_) continue;  // clock moved backwards
        const uint32_t age = todayOrdinal_ - record.dayOrdinal;
        if (age >= NewsIndex::DAY_COUNT) continue;  // older than the window we keep
        day = static_cast<uint8_t>(age);
      } else if (header.syncOrdinal != 0 && todayOrdinal_ != 0) {
        // Undated item: it belongs to whichever day the sync itself ran on.
        const uint32_t age = (todayOrdinal_ >= header.syncOrdinal) ? todayOrdinal_ - header.syncOrdinal : 0;
        if (age >= NewsIndex::DAY_COUNT) continue;
        day = static_cast<uint8_t>(age);
      }
      entries_.push_back(Entry{static_cast<uint8_t>(slot), i, day, record.epoch});
    }
  }
  // Merged storage lists this sync's items before the carried-over ones, so
  // order by feed, then day, then newest first to read chronologically.
  std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
    if (a.slot != b.slot) return a.slot < b.slot;
    if (a.day != b.day) return a.day < b.day;
    return a.epoch > b.epoch;
  });
  LOG_INF("NEWS", "Loaded %zu items across %zu slots", entries_.size(), slots);
}

void NewsActivity::refreshTopRows() {
  char buf[48];
  const uint32_t lastSync = NEWS_FEEDS.lastSyncEpoch();
  if (lastSync == 0 || !synced_) {
    syncValue_ = tr(STR_NEWS_NEVER_SYNCED);
  } else {
    const uint32_t syncDay = NewsDate::localOrdinalForEpoch(lastSync, localOffsetSec_);
    char clock[8];
    formatClock(lastSync, localOffsetSec_, clock, sizeof(clock));
    if (todayOrdinal_ != 0 && syncDay == todayOrdinal_) {
      snprintf(buf, sizeof(buf), "%s %s", tr(STR_NEWS_TODAY), clock);
    } else if (todayOrdinal_ != 0 && syncDay + 1 == todayOrdinal_) {
      snprintf(buf, sizeof(buf), "%s %s", tr(STR_NEWS_YESTERDAY), clock);
    } else {
      char date[28];
      TimeUtils::formatDayOrdinal(syncDay, date, sizeof(date));
      snprintf(buf, sizeof(buf), "%s %s", date, clock);
    }
    syncValue_ = buf;
  }

  snprintf(buf, sizeof(buf), "%zu", NEWS_FEEDS.enabledSlotCount());
  feedsValue_ = buf;

  // Tab labels track the real clock: Today, Yesterday, then a bare date, since
  // "the day before yesterday" does not fit a tab.
  for (size_t i = 0; i < NewsIndex::DAY_COUNT; i++) {
    if (i == 0) {
      tabLabels_[i] = tr(STR_NEWS_TODAY);
    } else if (i == 1) {
      tabLabels_[i] = tr(STR_NEWS_YESTERDAY);
    } else if (todayOrdinal_ > i) {
      char date[28];
      TimeUtils::formatDayOrdinal(todayOrdinal_ - static_cast<uint32_t>(i), date, sizeof(date));
      // "27 Aug 2026" -> "27 Aug": the year is noise on a three-day window.
      char* lastSpace = strrchr(date, ' ');
      if (lastSpace) *lastSpace = '\0';
      tabLabels_[i] = date;
    } else {
      tabLabels_[i].clear();
    }
  }

  topLabels_[0] = tr(STR_NEWS_SYNC);
  topLabels_[1] = tr(STR_NEWS_FEEDS);
  topItems_.clear();
  topItems_.reserve(2);
  for (int i = 0; i < 2; i++) {
    fui::ListItem item;
    item.label = topLabels_[i];
    item.value = (i == 0) ? syncValue_.c_str() : feedsValue_.c_str();
    item.actionValue = static_cast<int16_t>(i);
    topItems_.push_back(item);
  }
}

void NewsActivity::rebuildDayRows() {
  dayRows_.clear();
  uint8_t currentSlot = 0xFF;
  for (size_t i = 0; i < entries_.size(); i++) {
    if (entries_[i].day != tab_) continue;
    if (entries_[i].slot != currentSlot) {
      currentSlot = entries_[i].slot;
      dayRows_.push_back(DayRow{true, currentSlot, 0});
    }
    dayRows_.push_back(DayRow{false, entries_[i].slot, static_cast<uint16_t>(i)});
  }
  page_ = 0;
  paginate();
  loadPageLabels();
}

int NewsActivity::headerRowHeight() const {
  // FreeInkUI's own default for a header row; spelled out here because
  // paginate() has to measure the band without a Screen to ask.
  return renderer.getLineHeight(SMALL_FONT_ID) + 4;
}

int NewsActivity::sectionGapHeight() const {
  // Breathing room above each feed heading, so the last headline of one outlet
  // does not sit against the name of the next.
  return UITheme::getInstance().getMetrics().verticalSpacing * 2;
}

void NewsActivity::paginate() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowStep = metrics.listRowHeight + metrics.listRowGap;
  const int headerStep = headerRowHeight() + metrics.listRowGap;
  const int sectionGap = sectionGapHeight();
  // The band the day list gets: everything below the tab bar and above the hints.
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 2 * rowStep +
                      metrics.verticalSpacing + 1 + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  const int bandHeight = listBottom - listTop;

  pageStarts_.clear();
  pageStarts_.push_back(0);

  size_t index = 0;
  int used = 0;
  while (index < dayRows_.size()) {
    // The list pads every header except the one that opens a page, so the same
    // condition (used > 0) decides whether the gap is measured here.
    const int step = dayRows_[index].header ? headerStep + (used > 0 ? sectionGap : 0) : rowStep;
    if (used > 0 && used + step > bandHeight) {
      // A feed header stranded at the foot of a page moves down with its first
      // item, so a page never ends on a heading with nothing under it. The
      // pull-back is skipped when it would repeat the current page's start,
      // which would otherwise spin here on a band too short for header + row.
      size_t breakAt = index;
      if (dayRows_[index - 1].header && static_cast<int>(index) - 1 > pageStarts_.back()) breakAt = index - 1;
      pageStarts_.push_back(static_cast<int>(breakAt));
      index = breakAt;
      used = 0;
      continue;
    }
    used += step;
    index++;
  }
  pageStarts_.push_back(static_cast<int>(dayRows_.size()));
  if (page_ >= pageCount()) page_ = (pageCount() > 0) ? pageCount() - 1 : 0;
}

int NewsActivity::pageCount() const { return static_cast<int>(pageStarts_.size()) - 1; }

int NewsActivity::rowsOnPage() const {
  if (pageStarts_.size() < 2) return 0;
  const int page = (page_ < pageCount()) ? page_ : 0;
  return pageStarts_[page + 1] - pageStarts_[page];
}

void NewsActivity::loadPageLabels() {
  pageLabels_.clear();
  pageValues_.clear();
  pageItems_.clear();
  const int rows = rowsOnPage();
  if (rows <= 0) return;
  pageLabels_.reserve(rows);
  pageValues_.reserve(rows);
  pageItems_.reserve(rows);

  const int begin = pageStarts_[page_];
  // Titles live on SD, not in RAM: only the rows actually on screen are read,
  // so a fourteen-feed day costs a page of reads rather than 50KB of heap.
  HalFile index;
  int openSlot = -1;
  for (int i = 0; i < rows; i++) {
    const DayRow& row = dayRows_[begin + i];
    if (row.header) {
      pageLabels_.emplace_back(NEWS_FEEDS.slotName(row.slot));
      pageValues_.emplace_back();
      continue;
    }
    const Entry& entry = entries_[row.entry];
    if (openSlot != entry.slot) {
      if (index.isOpen()) index.close();
      char indexPath[64];
      NewsPaths::indexPath(entry.slot, indexPath, sizeof(indexPath));
      openSlot = Storage.openFileForRead("NEWS", indexPath, index) ? entry.slot : -1;
    }
    std::string title;
    if (openSlot == entry.slot &&
        index.seek(NewsIndex::HEADER_BYTES + static_cast<size_t>(entry.item) * NewsIndex::RECORD_BYTES)) {
      NewsIndex::Record record{};
      if (index.read(&record, sizeof(record)) == static_cast<int>(sizeof(record))) {
        record.title[NewsIndex::TITLE_BYTES - 1] = '\0';
        title = record.title;
      }
    }
    pageLabels_.push_back(std::move(title));
    char clock[8];
    formatClock(entry.epoch, localOffsetSec_, clock, sizeof(clock));
    pageValues_.emplace_back(clock);
  }
  if (index.isOpen()) index.close();

  for (int i = 0; i < rows; i++) {
    const DayRow& row = dayRows_[begin + i];
    fui::ListItem item;
    item.label = pageLabels_[i].c_str();
    item.actionValue = static_cast<int16_t>(FIXED_ROWS + i);
    if (row.header) {
      item.isHeader = true;
    } else if (!pageValues_[i].empty()) {
      item.value = pageValues_[i].c_str();
    }
    pageItems_.push_back(item);
  }
}

int NewsActivity::listCount() const { return FIXED_ROWS + rowsOnPage(); }

int NewsActivity::entryForCursor(const int cursor) const {
  const int offset = cursor - FIXED_ROWS;
  if (offset < 0 || offset >= rowsOnPage()) return -1;
  const DayRow& row = dayRows_[pageStarts_[page_] + offset];
  return row.header ? -1 : static_cast<int>(row.entry);
}

void NewsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int rowStep = metrics.listRowHeight + metrics.listRowGap;

  fui::ListProps top;
  top.items = topItems_.data();
  top.count = static_cast<uint16_t>(topItems_.size());
  top.action = ACTION_ROW;
  top.inputMask = fui::InputTouch;
  top.valueInset = 8;
  top.scrollIndicator = false;
  // Explicit, and the same value paginate() measures with: left at 0 the list
  // takes FreeInkUI's touch-sized default, which is taller than the settings
  // list and would push the Feeds row out of this fixed two-row band.
  top.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  top.rowGap = static_cast<int16_t>(metrics.listRowGap);
  top.selectedIndex = (nav.selected < 2) ? nav.selected : -1;
  screen.list(top, static_cast<int16_t>(2 * rowStep));

  // The rule and the tab bar are chrome (drawChrome paints them); the list only
  // has to leave the band empty so nothing renders underneath them.
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing + 1 + metrics.tabBarHeight + metrics.verticalSpacing));

  if (pageItems_.empty()) {
    const char* message = !synced_                             ? tr(STR_NEWS_NOT_SYNCED)
                          : NEWS_FEEDS.enabledSlotCount() == 0 ? tr(STR_NEWS_NO_FEEDS)
                                                               : tr(STR_NEWS_NO_ITEMS);
    screen.centeredText(message, screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = pageItems_.data();
  props.count = static_cast<uint16_t>(pageItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.balanceWrappedLabelWithValue = false;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.rowGap = static_cast<int16_t>(metrics.listRowGap);
  props.headerRowHeight = static_cast<int16_t>(headerRowHeight());
  props.sectionGap = static_cast<int16_t>(sectionGapHeight());
  props.selectedIndex = (nav.selected >= FIXED_ROWS) ? nav.selected - FIXED_ROWS : -1;
  // Paging is explicit here, so the list never scrolls itself.
  props.topIndex = 0;
  props.scrollIndicator = false;
  screen.list(props);
}

void NewsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  char title[32];
  if (pageCount() > 1) {
    snprintf(title, sizeof(title), "%s  %d/%d", tr(STR_NEWS), page_ + 1, pageCount());
  } else {
    snprintf(title, sizeof(title), "%s", tr(STR_NEWS));
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int rowStep = metrics.listRowHeight + metrics.listRowGap;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 2 * rowStep + metrics.verticalSpacing;
  renderer.fillRect(metrics.contentSidePadding, y, pageWidth - 2 * metrics.contentSidePadding, 1, true);
  // The tab bar sits straight under the rule: it carries its own padding, and
  // another gap here only pushed the headlines down the page.
  y += 1;

  std::vector<TabInfo> tabs;
  tabs.reserve(NewsIndex::DAY_COUNT);
  for (size_t i = 0; i < NewsIndex::DAY_COUNT; i++) {
    if (tabLabels_[i].empty()) continue;
    tabs.push_back({tabLabels_[i].c_str(), static_cast<int>(i) == tab_});
  }
  // The fourth argument inverts the active tab while the cursor rests on the
  // tab row, the same signal the settings screens use.
  GUI.drawTabBar(renderer, Rect{0, y, pageWidth, metrics.tabBarHeight}, tabs, nav.selected == ROW_TABS);
}

void NewsActivity::changeTab(const int delta) {
  RenderLock lock(*this);
  const int count = static_cast<int>(NewsIndex::DAY_COUNT);
  tab_ = ((tab_ + delta) % count + count) % count;
  rebuildDayRows();
  nav.selected = ROW_TABS;
}

void NewsActivity::changePage(const int delta) {
  if (pageCount() <= 1) return;
  RenderLock lock(*this);
  const int next = page_ + delta;
  if (next < 0 || next >= pageCount()) return;
  page_ = next;
  loadPageLabels();
  // Keep the cursor on the list rather than snapping back to the top rows.
  if (nav.selected >= FIXED_ROWS) {
    nav.selected = FIXED_ROWS;
    if (!dayRows_.empty() && dayRows_[pageStarts_[page_]].header) nav.selected = FIXED_ROWS + 1;
  }
}

bool NewsActivity::handleCustomInput() {
  // The side buttons page the day list. They are the reader's page keys, so
  // they honour the user's side-button swap setting.
  //
  // Only claimed when there is something to page to: those same buttons also
  // back ScrollNext/ScrollPrevious, which is how the cursor moves on every
  // other list in the firmware, and swallowing them on a single-page day would
  // leave only the front buttons able to move the selection.
  if (pageCount() <= 1) return false;

  // On release, and the press and any hold are swallowed, so the navigator
  // cannot also step the cursor with the press that turned the page.
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    changePage(1);
    requestUpdate();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    changePage(-1);
    requestUpdate();
    return true;
  }
  return mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
         mappedInput.isPressed(MappedInputManager::Button::PageBack);
}

int NewsActivity::nextSelectable(const int from, const int direction) const {
  // Feed headers are labels, not rows: walk past them.
  const int count = listCount();
  int index = from;
  for (int guard = 0; guard < count; guard++) {
    index = (direction > 0) ? ButtonNavigator::nextIndex(index, count) : ButtonNavigator::previousIndex(index, count);
    const int offset = index - FIXED_ROWS;
    if (offset < 0 || offset >= rowsOnPage() || !dayRows_[pageStarts_[page_] + offset].header) break;
  }
  return index;
}

void NewsActivity::navigateButtons() {
  const int count = listCount();
  if (count <= 0) return;
  // moveSelectionTo takes the render lock itself, and that mutex is not
  // recursive -- so the index is computed before it is called.
  buttonNavigator.onNextRelease([this] { moveSelectionTo(nextSelectable(nav.selected, 1)); });
  buttonNavigator.onPreviousRelease([this] { moveSelectionTo(nextSelectable(nav.selected, -1)); });
}

void NewsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  if (index == ROW_SYNC) {
    startActivityForResult(std::make_unique<NewsSyncActivity>(renderer, mappedInput), [](const ActivityResult&) {});
    return;
  }
  if (index == ROW_FEEDS) {
    startActivityForResult(std::make_unique<NewsFeedsActivity>(renderer, mappedInput), [this](const ActivityResult&) {
      RenderLock lock(*this);
      loadIndexes();
      refreshTopRows();
      rebuildDayRows();
    });
    return;
  }
  if (index == ROW_TABS) {
    changeTab(1);
    requestUpdate();
    return;
  }
  const int entryIndex = entryForCursor(index);
  if (entryIndex < 0) return;
  const Entry& entry = entries_[entryIndex];
  startActivityForResult(std::make_unique<NewsArticleActivity>(renderer, mappedInput, entry.slot, entry.item,
                                                               NEWS_FEEDS.slotName(entry.slot)),
                         [](const ActivityResult&) {});
}

void NewsActivity::drawFooter() {
  const char* confirmLabel = tr(STR_SELECT);
  if (nav.selected == ROW_SYNC) {
    confirmLabel = tr(STR_NEWS_SYNC);
  } else if (nav.selected == ROW_TABS) {
    // Name the tab Confirm would move to, not the one already highlighted.
    const int next = (tab_ + 1) % static_cast<int>(NewsIndex::DAY_COUNT);
    confirmLabel = tabLabels_[next].empty() ? tr(STR_SELECT) : tabLabels_[next].c_str();
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // The side buttons page the day list; same hint idiom as PrayerPickActivity.
  if (pageCount() > 1) {
    GUI.drawSideButtonHints(renderer, tr(STR_PAGE_UP), tr(STR_PAGE_DOWN));
  }
}
