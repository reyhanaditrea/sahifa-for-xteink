#include "activities/apps/NewsFeedsActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "NewsFeedStore.h"
#include "activities/apps/NewsAddFeedActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "news/NewsPaths.h"

namespace fui = freeink::ui;

namespace {
// What a menu entry does. Which entries are offered depends on the row, so the
// popup's index means nothing on its own and is mapped back through this.
enum MenuAction : uint8_t { ACTION_TOGGLE, ACTION_MOVE_UP, ACTION_MOVE_DOWN, ACTION_REMOVE };
}  // namespace

NewsFeedsActivity::NewsFeedsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("NewsFeeds", renderer, mappedInput) {}

void NewsFeedsActivity::onEnter() {
  UiListActivity::onEnter();
  RenderLock lock(*this);
  rebuildRows();
}

const char* NewsFeedsActivity::headerTitle() const { return tr(STR_NEWS_FEEDS); }

void NewsFeedsActivity::rebuildRows() {
  const size_t userCount = NEWS_FEEDS.userCount();
  const size_t builtinCount = NEWS_FEEDS.builtinCount();

  rows_.clear();
  labels_.clear();
  subtitles_.clear();
  items_.clear();
  const size_t total = 1 + (userCount > 0 ? 1 + userCount : 0) + 1 + builtinCount;
  rows_.reserve(total);
  labels_.reserve(total);
  subtitles_.reserve(userCount);
  items_.reserve(total);

  char buf[32];
  snprintf(buf, sizeof(buf), "%zu / %zu", userCount, NewsFeedStore::MAX_USER_FEEDS);
  addFeedValue_ = buf;

  labels_.emplace_back(tr(STR_NEWS_ADD_FEED));
  rows_.push_back({RowKind::AddFeed, 0});

  if (userCount > 0) {
    labels_.emplace_back(tr(STR_NEWS_YOUR_FEEDS));
    rows_.push_back({RowKind::SectionHeader, 0});
    for (size_t i = 0; i < userCount; i++) {
      const auto* feed = NEWS_FEEDS.userFeed(i);
      labels_.emplace_back(feed ? feed->name : "");
      subtitles_.emplace_back(feed ? feed->url : "");
      rows_.push_back({RowKind::UserFeed, i});
    }
  }

  labels_.emplace_back(tr(STR_NEWS_DEFAULT_OUTLETS));
  rows_.push_back({RowKind::SectionHeader, 0});
  for (size_t i = 0; i < builtinCount; i++) {
    // i is the position on screen; the outlet it shows is whatever the store's
    // order puts there.
    labels_.emplace_back(NewsBuiltins::FEEDS[NEWS_FEEDS.builtinAt(i)].name);
    rows_.push_back({RowKind::Builtin, i});
  }

  // Labels are stable now, so the borrowed pointers in items_ stay valid.
  size_t subtitleIndex = 0;
  for (size_t i = 0; i < rows_.size(); i++) {
    fui::ListItem item;
    item.label = labels_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    switch (rows_[i].kind) {
      case RowKind::AddFeed:
        item.value = addFeedValue_.c_str();
        break;
      case RowKind::SectionHeader:
        item.isHeader = true;
        break;
      case RowKind::UserFeed:
        item.subtitle = subtitles_[subtitleIndex++].c_str();
        item.toggle = true;
        item.toggleChecked = NEWS_FEEDS.userFeedEnabled(rows_[i].index);
        break;
      case RowKind::Builtin:
        item.toggle = true;
        item.toggleChecked = NEWS_FEEDS.builtinEnabled(NEWS_FEEDS.builtinAt(rows_[i].index));
        break;
    }
    items_.push_back(item);
  }

  // A header must never hold the cursor: nothing happens when it is confirmed.
  if (nav.selected < 0 || nav.selected >= static_cast<int>(rows_.size()) ||
      rows_[nav.selected].kind == RowKind::SectionHeader) {
    nav.selected = 0;
  }
}

void NewsFeedsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = items_.data();
  props.count = static_cast<uint16_t>(items_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  // User feeds carry their URL on a second line; the built-ins do not, but the
  // taller row height has to be uniform for the page arithmetic to hold.
  syncListViewport(screen, props, /*hasSubtitle=*/NEWS_FEEDS.userCount() > 0);
  screen.list(props);
}

int NewsFeedsActivity::nextSelectable(const int from, const int direction) const {
  // Section headers are labels, not rows: walk past them so the cursor never
  // lands somewhere Confirm would do nothing.
  const int count = listCount();
  int index = from;
  for (int guard = 0; guard < count; guard++) {
    index = (direction > 0) ? ButtonNavigator::nextIndex(index, count) : ButtonNavigator::previousIndex(index, count);
    if (rows_[index].kind != RowKind::SectionHeader) break;
  }
  return index;
}

bool NewsFeedsActivity::handleCustomInput() {
  return menuPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

void NewsFeedsActivity::render(RenderLock&& lock) {
  if (menuPopup_.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void NewsFeedsActivity::navigateButtons() {
  const int count = listCount();
  if (count <= 0) return;
  // moveSelectionTo takes the render lock itself, and that mutex is not
  // recursive -- so the index is computed before it is called.
  buttonNavigator.onNextRelease([this] { moveSelectionTo(nextSelectable(nav.selected, 1)); });
  buttonNavigator.onPreviousRelease([this] { moveSelectionTo(nextSelectable(nav.selected, -1)); });
}

void NewsFeedsActivity::invalidateCache() {
  // Slots are positional, so any change to the user list makes every cached
  // feedN.* file ambiguous. Cheaper and safer than renumbering on SD.
  Storage.removeDir(NewsPaths::DIR);
}

void NewsFeedsActivity::promptAddFeed() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NEWS_FEED_URL), "", 127,
                                                                 InputType::Url, tr(STR_NEWS_FEED_URL_TIP)),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto& text = std::get<KeyboardResult>(result.data).text;
                           if (text.empty()) return;
                           startActivityForResult(std::make_unique<NewsAddFeedActivity>(renderer, mappedInput, text),
                                                  [this](const ActivityResult&) {
                                                    RenderLock lock(*this);
                                                    rebuildRows();
                                                  });
                         });
}

void NewsFeedsActivity::promptRemove(const size_t userIndex) {
  const auto* feed = NEWS_FEEDS.userFeed(userIndex);
  if (!feed) return;
  const std::string name = feed->name;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_NEWS_REMOVE_FEED), name),
                         [this, userIndex](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             NEWS_FEEDS.removeUserFeed(userIndex);
                             invalidateCache();
                           }
                           RenderLock lock(*this);
                           rebuildRows();
                         });
}

size_t NewsFeedsActivity::slotForRow(const Row& row) const {
  return (row.kind == RowKind::UserFeed) ? NEWS_FEEDS.builtinCount() + row.index : row.index;
}

bool NewsFeedsActivity::swapCachedFeeds(const size_t slotA, const size_t slotB) {
  bool ok = true;
  for (int pass = 0; pass < 2; pass++) {
    char a[64];
    char b[64];
    char tmp[64];
    if (pass == 0) {
      NewsPaths::indexPath(slotA, a, sizeof(a));
      NewsPaths::indexPath(slotB, b, sizeof(b));
      NewsPaths::indexTempPath(slotA, tmp, sizeof(tmp));
    } else {
      NewsPaths::bodyPath(slotA, a, sizeof(a));
      NewsPaths::bodyPath(slotB, b, sizeof(b));
      NewsPaths::bodyTempPath(slotA, tmp, sizeof(tmp));
    }
    // Either side may have nothing cached -- a feed that has never synced, or
    // one that was off during the last sync -- so a missing file is not a
    // failure, it just makes that half of the swap a plain rename.
    const bool haveA = Storage.exists(a);
    const bool haveB = Storage.exists(b);
    if (!haveA && !haveB) continue;
    if (haveA && !Storage.rename(a, tmp)) {
      ok = false;
      continue;
    }
    if (haveB && !Storage.rename(b, a)) {
      ok = false;
      continue;
    }
    if (haveA && !Storage.rename(tmp, b)) ok = false;
  }
  return ok;
}

void NewsFeedsActivity::moveRow(const int index, const int delta) {
  if (index < 0 || index >= listCount()) return;
  const Row row = rows_[index];
  if (row.kind != RowKind::UserFeed && row.kind != RowKind::Builtin) return;
  const bool isUser = row.kind == RowKind::UserFeed;

  const size_t sectionCount = isUser ? NEWS_FEEDS.userCount() : NEWS_FEEDS.builtinCount();
  const size_t target = static_cast<size_t>(static_cast<int>(row.index) + delta);
  if (target >= sectionCount) return;

  const size_t slotA = slotForRow(row);
  const size_t slotB = slotForRow(Row{row.kind, target});

  // The in-memory move is what the rest of this pass reads, so it stands even
  // if persisting it failed; the cache has to follow it either way.
  if (isUser) {
    (void)NEWS_FEEDS.moveUserFeed(row.index, delta);
  } else {
    (void)NEWS_FEEDS.moveBuiltin(row.index, delta);
  }
  if (!swapCachedFeeds(slotA, slotB)) {
    LOG_ERR("NEWS", "slot %zu/%zu cache swap failed, clearing", slotA, slotB);
    invalidateCache();
  }
  // "Reopen this article after the restart" is a slot number, and both slots
  // just changed hands.
  NEWS_FEEDS.clearPendingArticle();

  {
    RenderLock lock(*this);
    // A section is contiguous, so the row that moved is exactly one list
    // position away -- follow it rather than leaving the cursor behind.
    nav.selected = index + delta;
    rebuildRows();
  }
  requestUpdate();
}

void NewsFeedsActivity::toggleRow(const Row& row) {
  if (row.kind == RowKind::UserFeed) {
    NEWS_FEEDS.setUserFeedEnabled(row.index, !NEWS_FEEDS.userFeedEnabled(row.index));
  } else if (row.kind == RowKind::Builtin) {
    const size_t table = NEWS_FEEDS.builtinAt(row.index);
    NEWS_FEEDS.setBuiltinEnabled(table, !NEWS_FEEDS.builtinEnabled(table));
  } else {
    return;
  }
  // Only changes what the next sync downloads; items already downloaded for a
  // feed switched off simply stop being listed.
  NEWS_FEEDS.saveToFile();
  {
    RenderLock lock(*this);
    rebuildRows();
  }
  requestUpdate();
}

int NewsFeedsActivity::rowIndexOf(const Row& row) const {
  for (size_t i = 0; i < rows_.size(); i++) {
    if (rows_[i].kind == row.kind && rows_[i].index == row.index) return static_cast<int>(i);
  }
  return -1;
}

void NewsFeedsActivity::showFeedMenu(const int index) {
  const Row row = rows_[index];
  const bool isUser = row.kind == RowKind::UserFeed;
  const size_t sectionCount = isUser ? NEWS_FEEDS.userCount() : NEWS_FEEDS.builtinCount();
  const bool enabled =
      isUser ? NEWS_FEEDS.userFeedEnabled(row.index) : NEWS_FEEDS.builtinEnabled(NEWS_FEEDS.builtinAt(row.index));

  // Entries are added only where they apply, so the popup's index means
  // nothing without the parallel action list.
  std::vector<const char*> options;
  std::vector<uint8_t> actions;
  options.push_back(enabled ? tr(STR_NEWS_DISABLE) : tr(STR_NEWS_ENABLE));
  actions.push_back(ACTION_TOGGLE);
  if (row.index > 0) {
    options.push_back(tr(STR_NEWS_MOVE_UP));
    actions.push_back(ACTION_MOVE_UP);
  }
  if (row.index + 1 < sectionCount) {
    options.push_back(tr(STR_NEWS_MOVE_DOWN));
    actions.push_back(ACTION_MOVE_DOWN);
  }
  if (isUser) {
    options.push_back(tr(STR_NEWS_REMOVE));
    actions.push_back(ACTION_REMOVE);
  }

  // Titled with the feed's own name: the popup covers the list, so nothing
  // else on screen would say which row is about to change. The row is captured
  // by value and looked up again on use -- a move rebuilds the list under it.
  menuPopup_.show(labels_[index].c_str(), options.data(), static_cast<int>(options.size()), 0,
                  [this, row, actions](const int choice) {
                    if (choice < 0 || choice >= static_cast<int>(actions.size())) return;
                    switch (actions[choice]) {
                      case ACTION_TOGGLE:
                        toggleRow(row);
                        break;
                      case ACTION_MOVE_UP:
                        moveRow(rowIndexOf(row), -1);
                        break;
                      case ACTION_MOVE_DOWN:
                        moveRow(rowIndexOf(row), 1);
                        break;
                      case ACTION_REMOVE:
                        promptRemove(row.index);
                        break;
                      default:
                        break;
                    }
                  });
  requestUpdate();
}

void NewsFeedsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  const Row row = rows_[index];
  switch (row.kind) {
    case RowKind::AddFeed:
      promptAddFeed();
      break;
    case RowKind::SectionHeader:
      break;
    case RowKind::UserFeed:
    case RowKind::Builtin:
      showFeedMenu(index);
      break;
  }
}

void NewsFeedsActivity::drawFooter() {
  const char* confirmLabel = tr(STR_SELECT);
  if (nav.selected >= 0 && nav.selected < listCount()) {
    switch (rows_[nav.selected].kind) {
      case RowKind::AddFeed:
        confirmLabel = tr(STR_ADD);
        break;
      case RowKind::UserFeed:
      case RowKind::Builtin:
        confirmLabel = tr(STR_NEWS_OPTIONS);
        break;
      case RowKind::SectionHeader:
        break;
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
