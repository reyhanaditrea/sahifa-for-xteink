#include "TextboundActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "TextboundStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Only used when a run starts without a seed of its own.
uint32_t hookEntropySeed() { return static_cast<uint32_t>(random(1, 0x7FFFFFFF)) ^ (millis() << 8); }

constexpr int LOG_ROWS = 3;

}  // namespace

void TextboundActivity::onEnter() {
  Activity::onEnter();

  game_ = makeUniqueNoThrow<tb::Game>();
  if (!game_) {
    LOG_ERR("TBOUND", "OOM: game %d B", static_cast<int>(sizeof(tb::Game)));
    finish();
    return;
  }
  game_->reset();

  TEXTBOUND_STORE.loadFromFile();
  mode_ = Mode::Title;
  selected_ = 0;
  currentPage_ = 0;
  runRecorded_ = false;
  lines_.reserve(24);
  backHandled_ = mappedInput.isPressed(MappedInputManager::Button::Back);
  paginate();
  requestUpdate();
}

void TextboundActivity::onExit() {
  // Whatever the player was in the middle of is already on the card: persist()
  // runs after every action, so there is nothing to flush here.
  game_.reset();
  lines_.clear();
  lines_.shrink_to_fit();
  pageStarts_.clear();
  pageStarts_.shrink_to_fit();
  Activity::onExit();
}

// ---------------------------------------------------------------- run control

void TextboundActivity::startNewRun() {
  tb::Game::Hooks hooks;
  hooks.entropySeed = &hookEntropySeed;
  game_->prepareRun(0, hooks);
  mode_ = Mode::Playing;
  selected_ = 0;
  runRecorded_ = false;
  afterAction();
}

void TextboundActivity::continueRun() {
  if (!TEXTBOUND_STORE.hasSavedRun()) return;
  if (!game_->fromSaveString(TEXTBOUND_STORE.getSavedRun().c_str())) {
    LOG_ERR("TBOUND", "Saved run did not load; dropping it");
    TEXTBOUND_STORE.clearSavedRun();
    game_->reset();
    paginate();
    requestUpdate();
    return;
  }
  mode_ = Mode::Playing;
  selected_ = 0;
  runRecorded_ = false;
  afterAction();
}

void TextboundActivity::abandonRun() {
  recordFinishedRun();
  game_->reset();
  mode_ = Mode::Title;
  selected_ = 0;
  afterAction();
}

void TextboundActivity::recordFinishedRun() {
  if (runRecorded_) return;
  const bool won = game_->phase() == tb::Game::P_WON;
  TEXTBOUND_STORE.reportRun(game_->state().depth, won);
  runRecorded_ = true;
}

void TextboundActivity::persist() {
  if (mode_ == Mode::Title) return;
  if (game_->runActive()) {
    char save[tb::Game::MAX_SAVE_LENGTH + 1];
    if (game_->toSaveString(save, sizeof(save)) > 0) TEXTBOUND_STORE.saveRun(save);
    return;
  }
  // Dead or escaped: the record is written once, and the run stops being resumable.
  if (game_->phase() == tb::Game::P_DEAD || game_->phase() == tb::Game::P_WON) recordFinishedRun();
}

void TextboundActivity::afterAction() {
  if (mode_ == Mode::Playing && game_->phase() == tb::Game::P_TITLE) {
    // The engine drops back to the title after death or the epilogue.
    mode_ = Mode::Title;
    selected_ = 0;
    runRecorded_ = false;
  }
  persist();
  paginate();
  const int rows = listRowCount();
  if (selected_ >= rows) selected_ = rows > 0 ? rows - 1 : 0;
  if (selected_ < 0) selected_ = 0;
  requestUpdate();
}

// ---------------------------------------------------------------- rows

int TextboundActivity::listRowCount() const {
  if (mode_ == Mode::Title) return TEXTBOUND_STORE.hasSavedRun() ? 2 : 1;
  return game_->rowCount();
}

std::string TextboundActivity::listRowText(int index) const {
  if (mode_ == Mode::Title) {
    if (index == TITLE_NEW) return tr(STR_TB_NEW_RUN);
    char label[64];
    snprintf(label, sizeof(label), "%s (%s %u)", tr(STR_TB_CONTINUE), tr(STR_TB_DEPTH),
             static_cast<unsigned>(TEXTBOUND_STORE.getBestDepth()));
    return std::string(label);
  }
  char label[96];
  game_->rowLabel(index, label, sizeof(label));
  return std::string(label);
}

bool TextboundActivity::listRowDimmed(int index) const {
  return mode_ == Mode::Title ? false : game_->rowDimmed(index);
}

// ---------------------------------------------------------------- input

void TextboundActivity::activateRow() {
  if (mode_ == Mode::Paused) {
    if (pauseSelected_ == 0) {
      mode_ = Mode::Playing;
      requestUpdate();
    } else {
      abandonRun();
    }
    return;
  }

  if (mode_ == Mode::Title) {
    if (selected_ == TITLE_NEW) {
      startNewRun();
    } else {
      continueRun();
    }
    return;
  }

  RenderLock lock(*this);
  game_->pick(selected_);
  currentPage_ = 0;
  selected_ = 0;
  afterAction();
}

void TextboundActivity::openPause() {
  mode_ = Mode::Paused;
  pauseSelected_ = 0;
  requestUpdate();
}

void TextboundActivity::loop() {
  if (!game_) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (backHandled_) {
      backHandled_ = false;
    } else if (mode_ == Mode::Paused) {
      mode_ = Mode::Playing;
      requestUpdate();
    } else if (mode_ == Mode::Playing && game_->canGoBack()) {
      RenderLock lock(*this);
      game_->goBack();
      selected_ = 0;
      afterAction();
    } else if (mode_ == Mode::Playing) {
      openPause();
    } else {
      finish();
      return;
    }
  }
  if (!mappedInput.isPressed(MappedInputManager::Button::Back)) backHandled_ = false;

  const int rows = (mode_ == Mode::Paused) ? 2 : listRowCount();
  buttonNavigator_.onScrollNextRelease([this, rows] {
    int& cursor = (mode_ == Mode::Paused) ? pauseSelected_ : selected_;
    cursor = ButtonNavigator::nextIndex(cursor, rows);
    requestUpdate();
  });
  buttonNavigator_.onScrollPreviousRelease([this, rows] {
    int& cursor = (mode_ == Mode::Paused) ? pauseSelected_ : selected_;
    cursor = ButtonNavigator::previousIndex(cursor, rows);
    requestUpdate();
  });

  // Front Left/Right page the narrative; the side buttons stay on the list, so
  // the cursor never moves under the player while they are reading.
  if (mode_ != Mode::Paused && pageCount() > 1) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Right) && currentPage_ + 1 < pageCount()) {
      RenderLock lock(*this);
      currentPage_++;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) && currentPage_ > 0) {
      RenderLock lock(*this);
      currentPage_--;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) activateRow();
}

// ---------------------------------------------------------------- layout

int TextboundActivity::pageCount() const {
  const int count = static_cast<int>(pageStarts_.size()) - 1;
  return count < 1 ? 1 : count;
}

void TextboundActivity::paginate() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int textWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  const bool playing = mode_ != Mode::Title;
  const bool showStatus = playing && game_->showsStatus();
  const bool showLog = playing && game_->showsLog() && game_->logCount() > 0;

  int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (showStatus) top += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing + 1;

  const int rows = listRowCount();
  const int listHeight = rows * GUI.getListRowStep(false);
  const int logHeight = showLog ? LOG_ROWS * renderer.getLineHeight(SMALL_FONT_ID) + 10 : 0;
  int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - listHeight;
  if (showLog) bottom -= logHeight + metrics.verticalSpacing;

  lines_.clear();
  pageStarts_.clear();

  const char* body = playing ? game_->narrative() : tb::kIntro;
  // The engine separates paragraphs with '\n'; each wraps on its own so the
  // break survives onto the screen.
  std::string text(body == nullptr ? "" : body);
  size_t start = 0;
  while (start <= text.size()) {
    const size_t newline = text.find('\n', start);
    const size_t end = (newline == std::string::npos) ? text.size() : newline;
    if (end > start) {
      auto wrapped = renderer.wrappedText(UI_10_FONT_ID, text.substr(start, end - start).c_str(), textWidth, 200);
      for (auto& line : wrapped) lines_.push_back(std::move(line));
    } else if (!lines_.empty()) {
      lines_.emplace_back();  // blank spacer between paragraphs
    }
    if (newline == std::string::npos) break;
    start = newline + 1;
  }

  const int available = bottom - top;
  int rowsPerPage = (available > 0 && lineHeight > 0) ? available / lineHeight : 1;
  if (rowsPerPage < 1) rowsPerPage = 1;

  pageStarts_.push_back(0);
  int line = rowsPerPage;
  const int total = static_cast<int>(lines_.size());
  while (line < total) {
    pageStarts_.push_back(line);
    line += rowsPerPage;
  }
  pageStarts_.push_back(total);
  if (currentPage_ >= pageCount()) currentPage_ = pageCount() - 1;
  if (currentPage_ < 0) currentPage_ = 0;
}

// ---------------------------------------------------------------- render

void TextboundActivity::render(RenderLock&&) {
  if (!game_) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int textWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  renderer.clearScreen();

  const bool playing = mode_ != Mode::Title;
  const bool showStatus = playing && game_->showsStatus();
  const bool showLog = playing && game_->showsLog() && game_->logCount() > 0;

  char subtitle[32];
  subtitle[0] = '\0';
  if (playing) {
    snprintf(subtitle, sizeof(subtitle), "%s %08lX", tr(STR_TB_SEED), static_cast<unsigned long>(game_->state().seed));
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TEXTBOUND),
                 playing ? subtitle : nullptr);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  if (showStatus) {
    const tb::Game::State& s = game_->state();
    char left[64];
    char right[64];
    snprintf(left, sizeof(left), "%s %u/%u   %s %u", tr(STR_TB_HP), s.hp, s.maxHp, tr(STR_TB_GOLD), s.gold);
    snprintf(right, sizeof(right), "%s %u   %s %u", tr(STR_TB_DEPTH), s.depth, tr(STR_TB_TURN), s.turn);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, left, true, EpdFontFamily::BOLD);
    const int rightWidth = renderer.getTextWidth(UI_10_FONT_ID, right, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, pageWidth - metrics.contentSidePadding - rightWidth, y, right, true,
                      EpdFontFamily::BOLD);
    y += lineHeight + metrics.verticalSpacing;
    renderer.fillRect(metrics.contentSidePadding, y, textWidth, 1, true);
    y += metrics.verticalSpacing;
  }

  const int rows = listRowCount();
  const int rowStep = GUI.getListRowStep(false);
  const int listHeight = rows * rowStep;
  const int logHeight = showLog ? LOG_ROWS * renderer.getLineHeight(SMALL_FONT_ID) + 10 : 0;
  const int listTop = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - listHeight;
  const int logTop = listTop - metrics.verticalSpacing - logHeight;

  // Narrative
  const int begin = pageStarts_.empty() ? 0 : pageStarts_[currentPage_];
  const int end = pageStarts_.empty() ? 0 : pageStarts_[currentPage_ + 1];
  for (int i = begin; i < end && i < static_cast<int>(lines_.size()); i++) {
    if (!lines_[i].empty()) renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, lines_[i].c_str());
    y += lineHeight;
  }

  if (pageCount() > 1) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%d / %d", currentPage_ + 1, pageCount());
    const int width = renderer.getTextWidth(SMALL_FONT_ID, counter);
    const int counterTop = (showLog ? logTop : listTop) - renderer.getLineHeight(SMALL_FONT_ID);
    renderer.drawText(SMALL_FONT_ID, pageWidth - metrics.contentSidePadding - width, counterTop, counter);
  }

  // Combat log: a fixed three-line box, so only the numbers change between
  // turns and the rest of the screen can stay put.
  if (showLog) {
    renderer.drawRect(metrics.contentSidePadding, logTop, textWidth, logHeight, true);
    int logY = logTop + 5;
    for (int i = 0; i < LOG_ROWS; i++) {
      const char* line = game_->logLine(i);
      if (line != nullptr && line[0] != '\0') {
        renderer.drawTextInWidth(SMALL_FONT_ID, metrics.contentSidePadding + 6, logY, textWidth - 12, line);
      }
      logY += renderer.getLineHeight(SMALL_FONT_ID);
    }
  }

  // The list is pinned above the hints, so the cursor is always in the same place.
  if (rows > 0) {
    GUI.drawList(
        renderer, Rect{0, listTop, pageWidth, listHeight}, rows, mode_ == Mode::Paused ? -1 : selected_,
        [this](int index) { return listRowText(index); }, nullptr, nullptr, nullptr, false,
        [this](int index) { return listRowDimmed(index); });
  }

  if (mode_ == Mode::Title) {
    // The record across runs. Nothing here carries into the next one.
    char line[64];
    int statsY = listTop - metrics.verticalSpacing - 3 * renderer.getLineHeight(SMALL_FONT_ID);
    snprintf(line, sizeof(line), "%s: %u", tr(STR_TB_BEST_DEPTH), TEXTBOUND_STORE.getBestDepth());
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, statsY, line);
    statsY += renderer.getLineHeight(SMALL_FONT_ID);
    snprintf(line, sizeof(line), "%s: %u", tr(STR_TB_RUNS), TEXTBOUND_STORE.getRuns());
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, statsY, line);
    statsY += renderer.getLineHeight(SMALL_FONT_ID);
    snprintf(line, sizeof(line), "%s: %u", tr(STR_TB_ESCAPED), TEXTBOUND_STORE.getEscaped());
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, statsY, line);
  }

  if (mode_ == Mode::Paused) {
    const int popupWidth = std::min(pageWidth - 4 * metrics.contentSidePadding, 320);
    const int popupHeight = 2 * rowStep + metrics.headerHeight;
    const Rect popup{(pageWidth - popupWidth) / 2, (pageHeight - popupHeight) / 2, popupWidth, popupHeight};
    renderer.fillRect(popup.x, popup.y, popup.width, popup.height, false);
    renderer.drawRect(popup.x, popup.y, popup.width, popup.height, 2, true);
    UITheme::drawCenteredText(renderer, popup, UI_12_FONT_ID, popup.y + metrics.verticalSpacing, tr(STR_PAUSED), true,
                              EpdFontFamily::BOLD);
    GUI.drawList(renderer, Rect{popup.x, popup.y + metrics.headerHeight, popup.width, 2 * rowStep}, 2, pauseSelected_,
                 [](int index) { return std::string(index == 0 ? tr(STR_RESUME) : tr(STR_TB_ABANDON)); });
  }

  const char* pagePrev = pageCount() > 1 ? tr(STR_TB_PAGE_PREV) : "";
  const char* pageNext = pageCount() > 1 ? tr(STR_TB_PAGE_NEXT) : "";
  const auto labels = mappedInput.mapLabels(mode_ == Mode::Title ? tr(STR_EXIT) : tr(STR_TB_MENU), tr(STR_TB_CHOOSE),
                                            pagePrev, pageNext);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
