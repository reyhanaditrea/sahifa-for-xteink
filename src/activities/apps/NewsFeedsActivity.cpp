#include "activities/apps/NewsFeedsActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "NewsFeedStore.h"
#include "activities/apps/NewsAddFeedActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "news/NewsPaths.h"

namespace fui = freeink::ui;

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
    labels_.emplace_back(NewsBuiltins::FEEDS[i].name);
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
        break;
      case RowKind::Builtin:
        item.toggle = true;
        item.toggleChecked = NEWS_FEEDS.builtinEnabled(rows_[i].index);
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
      promptRemove(row.index);
      break;
    case RowKind::Builtin: {
      // Toggling only changes what the next sync downloads; already-downloaded
      // items for a disabled outlet simply stop being listed.
      const bool enabled = !NEWS_FEEDS.builtinEnabled(row.index);
      NEWS_FEEDS.setBuiltinEnabled(row.index, enabled);
      NEWS_FEEDS.saveToFile();
      {
        RenderLock lock(*this);
        items_[index].toggleChecked = enabled;
      }
      requestUpdate();
      break;
    }
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
        confirmLabel = tr(STR_NEWS_REMOVE);
        break;
      case RowKind::Builtin:
        confirmLabel = tr(STR_NEWS_TOGGLE);
        break;
      case RowKind::SectionHeader:
        break;
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
