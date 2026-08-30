#include "activities/apps/NewsFullTextActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstring>

#include "MappedInputManager.h"
#include "NewsFeedStore.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "news/NewsArticleExtractor.h"
#include "news/NewsIndexIo.h"
#include "news/NewsPaths.h"

namespace {

// Room for the longest article measured across the built-in outlets (BBC 6.5KB,
// DW 6.1KB); past this the extractor stops with an ellipsis. Kept to the same
// budget the feed parser proved workable rather than the 12KB it started at:
// the TLS session underneath wants every spare byte.
constexpr size_t FULL_TEXT_BYTES = 8 * 1024;

// A TLS handshake needs a large contiguous block, and the article buffer sits
// alongside it. Below this the fetch is refused rather than risking the
// allocator giving out mid-handshake.
constexpr size_t MIN_FREE_BLOCK = 48 * 1024;

}  // namespace

bool NewsFullTextActivity::fetchInto(const size_t slot, const uint16_t itemIndex) {
  NewsIndex::Record record{};
  if (!NewsIndexIo::readRecord(slot, itemIndex, record)) return false;
  record.url[NewsIndex::URL_BYTES - 1] = '\0';
  if (record.url[0] == '\0') return false;

  if (ESP.getMaxAllocHeap() < MIN_FREE_BLOCK) {
    LOG_ERR("NEWS", "full text skipped: largest block %u below %zu", ESP.getMaxAllocHeap(), MIN_FREE_BLOCK);
    return false;
  }
  LOG_INF("NEWS", "full text: slot %zu item %u free=%u largest=%u url=%s", slot, itemIndex, ESP.getFreeHeap(),
          ESP.getMaxAllocHeap(), record.url);

  auto buffer = makeUniqueNoThrow<char[]>(FULL_TEXT_BYTES);
  if (!buffer) {
    LOG_ERR("NEWS", "OOM: %zu bytes for article text", FULL_TEXT_BYTES);
    return false;
  }

  size_t textLen = 0;
  const auto result = NewsArticleFetch::fetch(record.url, buffer.get(), FULL_TEXT_BYTES, textLen);
  if (result != NewsArticleFetch::Result::Ok || textLen == 0) {
    // Remembered per feed, not per item: the next article from the same outlet
    // would fail the same way, so the option stops being offered for it.
    record.flags = static_cast<uint8_t>((record.flags & ~NewsIndex::FLAG_QUEUED) | NewsIndex::FLAG_FAILED);
    NewsIndexIo::writeRecord(slot, itemIndex, record);
    NEWS_FEEDS.noteFullTextResult(slot, false);
    return false;
  }

  char bodyPath[64];
  NewsPaths::bodyPath(slot, bodyPath, sizeof(bodyPath));
  HalFile body = Storage.open(bodyPath, O_RDWR);
  if (!body) {
    LOG_ERR("NEWS", "slot %zu: no body file to append the article to", slot);
    return false;
  }
  // Appended past the summaries: the offsets already written must stay valid.
  const size_t offset = body.size();
  if (!body.seek(offset) || body.write(buffer.get(), textLen) != textLen) {
    LOG_ERR("NEWS", "slot %zu: article write failed", slot);
    body.close();
    return false;
  }
  body.close();

  record.fullOffset = static_cast<uint32_t>(offset);
  record.fullLen = static_cast<uint32_t>(textLen);
  record.flags = static_cast<uint8_t>(record.flags & ~(NewsIndex::FLAG_QUEUED | NewsIndex::FLAG_FAILED));
  if (!NewsIndexIo::writeRecord(slot, itemIndex, record)) return false;

  NEWS_FEEDS.noteFullTextResult(slot, true);
  LOG_INF("NEWS", "slot %zu item %u: stored %u bytes of article text", slot, itemIndex, static_cast<unsigned>(textLen));
  return true;
}

NewsFullTextActivity::NewsFullTextActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const size_t slot,
                                           const uint16_t itemIndex, std::string feedName)
    : Activity("NewsFullText", renderer, mappedInput),
      slot_(slot),
      itemIndex_(itemIndex),
      feedName_(std::move(feedName)) {}

void NewsFullTextActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void NewsFullTextActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToNews();
  }
}

void NewsFullTextActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    // The pending article is deliberately left set: dropping Wi-Fi still ends
    // in a restart, and the reader should come back to what they were reading
    // rather than to the top of the list.
    finish();
    return;
  }
  {
    RenderLock lock(*this);
    state_ = FETCHING;
  }
  requestUpdateAndWait();
  fetch();
}

void NewsFullTextActivity::fetch() {
  const bool ok = fetchInto(slot_, itemIndex_);
  RenderLock lock(*this);
  state_ = ok ? COMPLETE : ERROR;
}

void NewsFullTextActivity::loop() {
  if (state_ == COMPLETE || state_ == ERROR) {
    // Release, not press: the screen this returns to acts on the release, and
    // would otherwise take the same button event as its own dismissal.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}

void NewsFullTextActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int textWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, feedName_.c_str());

  const int centerY = (pageHeight - lineHeight) / 2;
  if (state_ == FETCHING) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_NEWS_FETCHING_FULL), true, EpdFontFamily::BOLD);
  } else if (state_ == COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_NEWS_FULL_DONE), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredTextWrapped(UI_10_FONT_ID, centerY - lineHeight, textWidth, tr(STR_NEWS_FULL_FAILED), 2, true,
                                     EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
