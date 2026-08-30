#include "activities/apps/NewsArticleActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <ScriptDetector.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "NewsFeedStore.h"
#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "news/NewsIndexIo.h"
#include "news/NewsPaths.h"

namespace {

// A feed that already ships the whole post -- WordPress and Substack put it in
// content:encoded, which the parser prefers -- has nothing left to fetch, and
// scraping its page would only re-derive the same words less cleanly. Summaries
// from the built-in outlets top out near 600 bytes, so this threshold only ever
// matches a genuine full-text feed, which is nearly always a user-added one.
constexpr uint32_t SUMMARY_IS_ENOUGH = 2048;

}  // namespace

NewsArticleActivity::NewsArticleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const size_t slot,
                                         const uint16_t itemIndex, std::string feedName)
    : Activity("NewsArticle", renderer, mappedInput),
      slot_(slot),
      itemIndex_(itemIndex),
      feedName_(std::move(feedName)) {}

bool NewsArticleActivity::loadRecord() {
  char indexPath[64];
  NewsPaths::indexPath(slot_, indexPath, sizeof(indexPath));

  HalFile index;
  if (!Storage.openFileForRead("NEWS", indexPath, index)) return false;

  NewsIndex::Header header{};
  if (index.read(&header, sizeof(header)) != static_cast<int>(sizeof(header))) return false;
  if (header.magic != NewsIndex::MAGIC || header.version != NewsIndex::VERSION) return false;
  if (itemIndex_ >= header.count) return false;

  if (!index.seek(NewsIndex::HEADER_BYTES + static_cast<size_t>(itemIndex_) * NewsIndex::RECORD_BYTES)) return false;
  // memcpy through a local: the record comes straight off an SD read and the
  // C3 faults on unaligned wide loads.
  if (index.read(&record_, sizeof(record_)) != static_cast<int>(sizeof(record_))) return false;
  record_.title[NewsIndex::TITLE_BYTES - 1] = '\0';
  index.close();

  record_.url[NewsIndex::URL_BYTES - 1] = '\0';
  // A fetched article opens with the same sentences the summary holds, so it
  // replaces the summary rather than being shown after it.
  const bool haveFull = record_.fullLen > 0;
  const uint32_t offset = haveFull ? record_.fullOffset : record_.bodyOffset;
  const uint32_t length = haveFull ? record_.fullLen : record_.bodyLen;

  if (record_.url[0] == '\0' || haveFull || record_.bodyLen >= SUMMARY_IS_ENOUGH) {
    action_ = Action::None;
  } else if ((record_.flags & NewsIndex::FLAG_QUEUED) != 0) {
    action_ = Action::Queued;
  } else if (!NEWS_FEEDS.fullTextOffered(slot_)) {
    action_ = Action::Blocked;
  } else {
    action_ = Action::Fetch;
  }

  if (length == 0) return true;

  char bodyPath[64];
  NewsPaths::bodyPath(slot_, bodyPath, sizeof(bodyPath));
  HalFile body;
  if (!Storage.openFileForRead("NEWS", bodyPath, body)) return true;
  auto buffer = makeUniqueNoThrow<char[]>(length + 1);
  if (!buffer) {
    LOG_ERR("NEWS", "OOM: %u bytes for article body", length);
    return true;
  }
  if (!body.seek(offset)) return true;
  const int read = body.read(buffer.get(), length);
  if (read > 0) {
    buffer[read] = '\0';
    body_.assign(buffer.get(), static_cast<size_t>(read));
  }
  return true;
}

int NewsArticleActivity::actionRowHeight() const {
  if (action_ == Action::None) return 0;
  const auto& metrics = UITheme::getInstance().getMetrics();
  return renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;
}

void NewsArticleActivity::paginate() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int textWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  // The title band only exists on page one, so the first page holds fewer body
  // lines than the rest.
  const int titleHeight =
      renderer.measureWrappedTextHeight(UI_12_FONT_ID, textWidth, record_.title, 3) + metrics.verticalSpacing * 2;
  const int bodyTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  // The action row is only drawn on the last page, but every page reserves its
  // height: pagination that changed shape on the final page would reflow the
  // whole article the moment the row appeared.
  const int bodyBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - actionRowHeight();
  const int fullHeight = bodyBottom - bodyTop;

  // Decided once per article, not per line: a mostly-Arabic body with a Latin
  // name in it still reads right-to-left.
  rtlBody_ =
      I18N.isRtl() || ScriptDetector::containsArabic(body_.c_str()) || ScriptDetector::containsArabic(record_.title);

  lines_.clear();
  if (body_.empty()) {
    lines_.emplace_back(tr(STR_NEWS_NO_BODY));
  } else {
    // The sink separates paragraphs with '\n'; each one wraps on its own so a
    // paragraph break survives into the rendered page.
    size_t start = 0;
    while (start <= body_.size()) {
      const size_t nl = body_.find('\n', start);
      const size_t end = (nl == std::string::npos) ? body_.size() : nl;
      if (end > start) {
        const std::string paragraph = body_.substr(start, end - start);
        auto wrapped = renderer.wrappedText(UI_10_FONT_ID, paragraph.c_str(), textWidth, 400);
        for (auto& line : wrapped) lines_.push_back(std::move(line));
      }
      if (nl == std::string::npos) break;
      // A blank spacer line keeps paragraphs apart on screen.
      lines_.emplace_back();
      start = nl + 1;
    }
  }

  pageStarts_.clear();
  pageStarts_.push_back(0);
  int line = 0;
  const int total = static_cast<int>(lines_.size());
  while (line < total) {
    const int available = (pageStarts_.size() == 1) ? (fullHeight - titleHeight) : fullHeight;
    int rows = available / lineHeight;
    if (rows < 1) rows = 1;
    line += rows;
    if (line < total) pageStarts_.push_back(line);
  }
  pageStarts_.push_back(total);
  currentPage_ = 0;
}

void NewsArticleActivity::onEnter() {
  Activity::onEnter();
  loaded_ = loadRecord();
  paginate();
  requestUpdate();
}

void NewsArticleActivity::openActionMenu() {
  // Fetching now costs a Wi-Fi connection and the restart that follows it;
  // queueing rides along with the next sync, which is already online.
  const char* options[] = {tr(STR_NEWS_FULL_NOW), tr(STR_NEWS_FULL_LATER), tr(STR_CANCEL)};
  actionPopup_.show(tr(STR_NEWS_GET_FULL), options, 3, 0, [this](const int index) {
    if (index == 0) {
      // Recorded, then handed to a reboot rather than run here. The fetch is
      // Wi-Fi plus a TLS session plus a 130-440KB page streaming through, and
      // on top of the list and this screen it had ~49KB of heap and aborted
      // out of memory inside the HTTP response parser. Booting into it first
      // gives it a heap nothing else has touched; the pending article is what
      // brings the reader back here when it is done.
      NEWS_FEEDS.setPendingArticle(slot_, itemIndex_);
      NEWS_FEEDS.setFullTextPending(true);
      silentRestartToNewsFullText();
      return;
    }
    if (index == 1) queueForSync(true);
    requestUpdate();
  });
  requestUpdate();
}

void NewsArticleActivity::queueForSync(const bool queued) {
  const uint8_t set = queued ? NewsIndex::FLAG_QUEUED : 0;
  const uint8_t clear = queued ? 0 : NewsIndex::FLAG_QUEUED;
  if (!NewsIndexIo::updateFlags(slot_, itemIndex_, set, clear)) return;
  RenderLock lock(*this);
  record_.flags = static_cast<uint8_t>((record_.flags & ~clear) | set);
  action_ = queued ? Action::Queued : Action::Fetch;
}

void NewsArticleActivity::loop() {
  if (actionPopup_.isActive()) {
    actionPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  // Release, not press: the list underneath closes on the Back release, so
  // finishing on the press would hand it that release and pop the list too.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const int lastPage = static_cast<int>(pageStarts_.size()) - 2;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && currentPage_ >= lastPage) {
    if (action_ == Action::Fetch) {
      openActionMenu();
      return;
    }
    if (action_ == Action::Queued) {
      // Confirm on a queued item takes it back out of the queue.
      queueForSync(false);
      requestUpdate();
      return;
    }
    if (action_ == Action::Blocked) {
      // The only way back from Blocked. A built-in can also be reset by
      // toggling it off and on, but a user feed has no toggle -- without this
      // its verdict would stand until the feed was removed and re-added, even
      // after the site changed or the two failures were a bad network day.
      NEWS_FEEDS.clearFullTextState(slot_);
      {
        RenderLock lock(*this);
        action_ = Action::Fetch;
      }
      openActionMenu();
      return;
    }
  }

  const int pageCount = static_cast<int>(pageStarts_.size()) - 1;
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                    mappedInput.wasReleased(MappedInputManager::Button::NavNext);
  const bool previous = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                        mappedInput.wasReleased(MappedInputManager::Button::NavPrevious);
  if (next && currentPage_ + 1 < pageCount) {
    RenderLock lock(*this);
    currentPage_++;
    requestUpdate();
  } else if (previous && currentPage_ > 0) {
    RenderLock lock(*this);
    currentPage_--;
    requestUpdate();
  }
}

void NewsArticleActivity::drawAligned(const int fontId, const int y, const char* text,
                                      const EpdFontFamily::Style style) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  if (!rtlBody_) {
    renderer.drawText(fontId, metrics.contentSidePadding, y, text, true, style);
    return;
  }
  // Arabic reads from the right margin, so the ragged edge belongs on the left.
  // Anchored per article rather than per line: a Latin-only line inside an
  // Arabic story still belongs to the right-hand column.
  const int width = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, pageWidth - metrics.contentSidePadding - width, y, text, true, style);
}

void NewsArticleActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int textWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, feedName_.c_str());

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (!loaded_) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - lineHeight) / 2, tr(STR_NEWS_NO_ITEMS));
    renderer.displayBuffer();
    return;
  }

  if (currentPage_ == 0) {
    // Wrapped by hand rather than through drawCenteredTextWrapped: the title
    // follows the body's alignment, and there is no centred-or-not variant.
    const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, record_.title, textWidth, 3, EpdFontFamily::BOLD);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    for (const auto& line : titleLines) {
      drawAligned(UI_12_FONT_ID, y, line.c_str(), EpdFontFamily::BOLD);
      y += titleLineHeight;
    }
    y += metrics.verticalSpacing;
    renderer.fillRect(metrics.contentSidePadding, y, textWidth, 1, true);
    y += metrics.verticalSpacing;
  }

  const int begin = pageStarts_[currentPage_];
  const int end = pageStarts_[currentPage_ + 1];
  for (int i = begin; i < end && i < static_cast<int>(lines_.size()); i++) {
    if (!lines_[i].empty()) drawAligned(UI_10_FONT_ID, y, lines_[i].c_str());
    y += lineHeight;
  }

  const int pageCount = static_cast<int>(pageStarts_.size()) - 1;
  const bool lastPage = currentPage_ + 1 >= pageCount;

  // The offer sits at the foot of the last page, where the summary runs out and
  // the reader is left wanting the rest.
  const char* actionLabel = nullptr;
  if (lastPage) {
    if (action_ == Action::Fetch) {
      actionLabel = tr(STR_NEWS_GET_FULL);
    } else if (action_ == Action::Queued) {
      actionLabel = tr(STR_NEWS_FULL_QUEUED);
    } else if (action_ == Action::Blocked) {
      actionLabel = tr(STR_NEWS_FULL_BLOCKED);
    }
  }
  if (actionLabel) {
    const int rowHeight = actionRowHeight();
    const int rowTop = pageHeight - metrics.buttonHintsHeight - rowHeight;
    renderer.fillRect(metrics.contentSidePadding, rowTop, textWidth, 1, true);
    // Blocked is a statement, not a button, so only the actionable rows are
    // drawn in bold.
    const bool actionable = action_ != Action::Blocked;
    renderer.drawCenteredText(UI_10_FONT_ID, rowTop + metrics.verticalSpacing, actionLabel, true,
                              actionable ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  char pageLabel[24] = {};
  if (pageCount > 1) {
    snprintf(pageLabel, sizeof(pageLabel), "%d / %d", currentPage_ + 1, pageCount);
  }
  const char* confirmLabel = pageLabel;
  if (lastPage && action_ == Action::Fetch) {
    confirmLabel = tr(STR_NEWS_GET_FULL);
  } else if (lastPage && action_ == Action::Queued) {
    confirmLabel = tr(STR_NEWS_FULL_UNQUEUE);
  } else if (lastPage && action_ == Action::Blocked) {
    confirmLabel = tr(STR_RETRY);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, pageCount > 1 ? tr(STR_PREV_PAGE) : "",
                                            pageCount > 1 ? tr(STR_NEXT_PAGE) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Drawn last so it sits over the page, and it owns the display flush.
  if (actionPopup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}
