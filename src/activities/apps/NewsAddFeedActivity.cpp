#include "activities/apps/NewsAddFeedActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>

#include "MappedInputManager.h"
#include "NewsFeedStore.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "news/NewsDiscovery.h"

NewsAddFeedActivity::NewsAddFeedActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string input)
    : Activity("NewsAddFeed", renderer, mappedInput), input_(std::move(input)) {}

void NewsAddFeedActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void NewsAddFeedActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToNews();
  }
}

void NewsAddFeedActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }
  {
    RenderLock lock(*this);
    state_ = DISCOVERING;
  }
  requestUpdateAndWait();
  discover();
}

void NewsAddFeedActivity::discover() {
  char feedUrl[160] = {};
  const auto result = NewsDiscovery::discoverFeedUrl(input_.c_str(), feedUrl, sizeof(feedUrl));
  if (result != NewsDiscovery::Result::Ok) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = tr(STR_NEWS_FEED_NOT_FOUND);
    return;
  }

  if (NEWS_FEEDS.hasUrl(feedUrl)) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = tr(STR_NEWS_FEED_DUPLICATE);
    return;
  }

  // Host name until the first sync reads the feed's own <title>.
  char host[64] = {};
  if (!NewsDiscovery::urlHost(feedUrl, host, sizeof(host))) {
    snprintf(host, sizeof(host), "%s", feedUrl);
  }

  if (!NEWS_FEEDS.addUserFeed(host, feedUrl)) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = tr(STR_NEWS_FEEDS_FULL);
    return;
  }

  LOG_INF("NEWS", "Added feed %s", feedUrl);
  RenderLock lock(*this);
  resolvedUrl_ = feedUrl;
  state_ = COMPLETE;
}

void NewsAddFeedActivity::loop() {
  if (state_ == COMPLETE || state_ == ERROR) {
    // Release, not press: the screen this returns to acts on the release, and
    // would otherwise take the same button event as its own dismissal.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}

void NewsAddFeedActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int textWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NEWS_ADD_FEED));

  const int centerY = (pageHeight - lineHeight) / 2;
  if (state_ == DISCOVERING) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_NEWS_FINDING_FEED), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredTextWrapped(UI_10_FONT_ID, centerY + lineHeight / 2, textWidth, input_.c_str(), 2);
  } else if (state_ == COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_NEWS_FEED_ADDED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredTextWrapped(UI_10_FONT_ID, centerY + lineHeight / 2, textWidth, resolvedUrl_.c_str(), 2);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredTextWrapped(UI_10_FONT_ID, centerY - lineHeight, textWidth,
                                     errorMessage_ ? errorMessage_ : tr(STR_NEWS_FEED_NOT_FOUND), 2, true,
                                     EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
