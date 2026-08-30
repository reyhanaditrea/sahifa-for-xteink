#include "activities/apps/NewsSyncActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "MappedInputManager.h"
#include "NewsFeedStore.h"
#include "SilentRestart.h"
#include "activities/apps/NewsFullTextActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "news/NewsDate.h"
#include "news/NewsFeedParser.h"
#include "news/NewsIndexIo.h"
#include "news/NewsPaths.h"
#include "util/TimeUtils.h"

namespace {

// A feed larger than this is a site error page or a full-text archive, not a
// daily headline list; skipping it protects the SD and the parse budget.
constexpr size_t MAX_FEED_BYTES = 512 * 1024;
// Below this the parser cannot get its 8KB body buffer plus expat's own state.
constexpr size_t MIN_FREE_BLOCK = 24 * 1024;
// Repaint every N parse steps so the progress screen stays alive without
// spending the whole sync on the display.
constexpr int STEPS_PER_REPAINT = 64;
// A slot accumulates across syncs, so it holds more than one fetch's worth.
constexpr uint16_t MAX_STORED_ITEMS = 90;

// Cheap identity for de-duplication across syncs. Feeds reissue the same item
// with the same title and timestamp, and the title is the only field every
// dialect reliably provides, so hashing it beats comparing links.
uint32_t itemKey(const NewsIndex::Record& record) {
  uint32_t hash = 2166136261u;  // FNV-1a
  for (size_t i = 0; i < NewsIndex::TITLE_BYTES && record.title[i]; i++) {
    hash = (hash ^ static_cast<unsigned char>(record.title[i])) * 16777619u;
  }
  return hash ^ record.epoch;
}

}  // namespace

NewsSyncActivity::NewsSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("NewsSync", renderer, mappedInput) {}

void NewsSyncActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void NewsSyncActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    // Back to News with a clean heap: parsing has churned the allocator.
    silentRestartToNews();
  }
}

void NewsSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }
  {
    RenderLock lock(*this);
    state_ = SYNCING;
  }
  requestUpdateAndWait();
  syncAll();
}

void NewsSyncActivity::pruneCache(const size_t slotCount) {
  Storage.mkdir(NewsPaths::DIR);
  Storage.remove(NewsPaths::TMP_FEED);
  // Scratch files from a run that was interrupted before its rename, plus any
  // slot the store no longer has. Everything else is kept: most feeds only
  // carry a day or two, so the older tabs are only ever filled by items an
  // earlier sync stored.
  char path[64];
  for (size_t slot = 0; slot < NewsFeedStore::MAX_SLOTS; slot++) {
    NewsPaths::indexTempPath(slot, path, sizeof(path));
    Storage.remove(path);
    NewsPaths::bodyTempPath(slot, path, sizeof(path));
    Storage.remove(path);
    if (slot < slotCount) continue;
    NewsPaths::indexPath(slot, path, sizeof(path));
    Storage.remove(path);
    NewsPaths::bodyPath(slot, path, sizeof(path));
    Storage.remove(path);
  }
}

uint16_t NewsSyncActivity::mergePreviousItems(const size_t slot, const char* indexTmp, const char* bodyTmp,
                                              const uint16_t freshCount) {
  char indexPath[64];
  char bodyPath[64];
  NewsPaths::indexPath(slot, indexPath, sizeof(indexPath));
  NewsPaths::bodyPath(slot, bodyPath, sizeof(bodyPath));

  HalFile oldIndex;
  if (!Storage.openFileForRead("NEWS", indexPath, oldIndex)) return freshCount;
  NewsIndex::Header oldHeader{};
  if (oldIndex.read(&oldHeader, sizeof(oldHeader)) != static_cast<int>(sizeof(oldHeader))) return freshCount;
  if (oldHeader.magic != NewsIndex::MAGIC || oldHeader.version != NewsIndex::VERSION) return freshCount;
  if (oldHeader.count == 0) return freshCount;

  // Keys of what this sync just wrote, so a reissued item is not stored twice.
  // 90 keys is 360 bytes -- cheaper than seeking the fresh index per old item.
  auto freshKeys = makeUniqueNoThrow<uint32_t[]>(MAX_STORED_ITEMS);
  if (!freshKeys) {
    LOG_ERR("NEWS", "OOM: merge key table");
    return freshCount;
  }
  uint16_t freshKeyCount = 0;
  {
    HalFile freshIndex;
    if (!Storage.openFileForRead("NEWS", indexTmp, freshIndex)) return freshCount;
    if (!freshIndex.seek(NewsIndex::HEADER_BYTES)) return freshCount;
    for (uint16_t i = 0; i < freshCount && freshKeyCount < MAX_STORED_ITEMS; i++) {
      NewsIndex::Record record{};
      if (freshIndex.read(&record, sizeof(record)) != static_cast<int>(sizeof(record))) break;
      freshKeys[freshKeyCount++] = itemKey(record);
    }
  }

  HalFile oldBody;
  const bool haveOldBody = Storage.openFileForRead("NEWS", bodyPath, oldBody);

  // O_RDWR, not openFileForWrite: that one adds O_TRUNC, which would throw away
  // the fresh items this sync just wrote into these very files.
  HalFile mergedIndex = Storage.open(indexTmp, O_RDWR);
  HalFile mergedBody = Storage.open(bodyTmp, O_RDWR);
  if (!mergedIndex || !mergedBody) {
    LOG_ERR("NEWS", "slot %zu could not reopen scratch files for merge", slot);
    return freshCount;
  }
  // Append after what the parser wrote.
  if (!mergedIndex.seek(NewsIndex::HEADER_BYTES + static_cast<size_t>(freshCount) * NewsIndex::RECORD_BYTES)) {
    return freshCount;
  }
  const size_t bodyEnd = mergedBody.size();
  if (!mergedBody.seek(bodyEnd)) return freshCount;

  uint16_t total = freshCount;
  uint32_t bodyOffset = static_cast<uint32_t>(bodyEnd);
  auto buffer = makeUniqueNoThrow<char[]>(8192);
  if (!buffer) {
    LOG_ERR("NEWS", "OOM: merge body buffer");
    return freshCount;
  }

  for (uint16_t i = 0; i < oldHeader.count && total < MAX_STORED_ITEMS; i++) {
    NewsIndex::Record record{};
    if (!oldIndex.seek(NewsIndex::HEADER_BYTES + static_cast<size_t>(i) * NewsIndex::RECORD_BYTES)) break;
    if (oldIndex.read(&record, sizeof(record)) != static_cast<int>(sizeof(record))) break;

    // Outside the window we keep, or already in this sync's results.
    bool wanted = (record.dayOrdinal == 0 && keepOrdinals_[0] == 0);
    for (const uint32_t keep : keepOrdinals_) {
      if (keep != 0 && keep == record.dayOrdinal) wanted = true;
    }
    if (!wanted) continue;
    const uint32_t key = itemKey(record);
    bool duplicate = false;
    for (uint16_t k = 0; k < freshKeyCount; k++) {
      if (freshKeys[k] == key) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    const auto copyRun = [&](const uint32_t from, const uint32_t length) {
      uint32_t copied = 0;
      if (!haveOldBody || length == 0 || !oldBody.seek(from)) return copied;
      uint32_t remaining = length;
      while (remaining > 0) {
        const size_t chunk = (remaining < 8192) ? remaining : 8192;
        const int read = oldBody.read(buffer.get(), chunk);
        if (read <= 0) break;
        if (mergedBody.write(buffer.get(), static_cast<size_t>(read)) != static_cast<size_t>(read)) break;
        copied += static_cast<uint32_t>(read);
        remaining -= static_cast<uint32_t>(read);
      }
      return copied;
    };

    const uint32_t copied = copyRun(record.bodyOffset, record.bodyLen);
    record.bodyOffset = bodyOffset;
    record.bodyLen = copied;
    bodyOffset += copied;

    // An article the user paid a Wi-Fi round trip for moves across with its
    // item; without this the next morning's sync would silently drop it.
    const uint32_t copiedFull = copyRun(record.fullOffset, record.fullLen);
    record.fullOffset = bodyOffset;
    record.fullLen = copiedFull;
    bodyOffset += copiedFull;
    if (mergedIndex.write(&record, sizeof(record)) != sizeof(record)) break;
    total++;
  }

  const NewsIndex::Header header{NewsIndex::MAGIC, NewsIndex::VERSION, total, keepOrdinals_[0]};
  if (!mergedIndex.seek(0) || mergedIndex.write(&header, sizeof(header)) != sizeof(header)) {
    LOG_ERR("NEWS", "slot %zu merge header rewrite failed", slot);
    return freshCount;
  }
  LOG_INF("NEWS", "slot %zu merged %u previous into %u fresh", slot, total - freshCount, freshCount);
  return total;
}

int NewsSyncActivity::syncOneFeed(const size_t slot) {
  const char* url = NEWS_FEEDS.slotUrl(slot);
  if (!url || url[0] == '\0') return -1;

  if (ESP.getMaxAllocHeap() < MIN_FREE_BLOCK) {
    LOG_ERR("NEWS", "slot %zu skipped: largest block %u", slot, ESP.getMaxAllocHeap());
    return -1;
  }

  Storage.remove(NewsPaths::TMP_FEED);
  if (HttpDownloader::downloadToFile(url, NewsPaths::TMP_FEED) != HttpDownloader::OK) {
    LOG_ERR("NEWS", "slot %zu download failed", slot);
    Storage.remove(NewsPaths::TMP_FEED);
    return -1;
  }

  {
    HalFile probe;
    if (Storage.openFileForRead("NEWS", NewsPaths::TMP_FEED, probe) && probe.size() > MAX_FEED_BYTES) {
      LOG_ERR("NEWS", "slot %zu feed too large (%zu bytes)", slot, probe.size());
      probe.close();
      Storage.remove(NewsPaths::TMP_FEED);
      return -1;
    }
  }

  char indexTmp[64];
  char bodyTmp[64];
  char indexPath[64];
  char bodyPath[64];
  NewsPaths::indexTempPath(slot, indexTmp, sizeof(indexTmp));
  NewsPaths::bodyTempPath(slot, bodyTmp, sizeof(bodyTmp));
  NewsPaths::indexPath(slot, indexPath, sizeof(indexPath));
  NewsPaths::bodyPath(slot, bodyPath, sizeof(bodyPath));
  Storage.remove(indexTmp);
  Storage.remove(bodyTmp);

  LOG_INF("NEWS", "slot %zu parse start free=%u largest=%u", slot, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  NewsFeedParser parser;
  if (!parser.begin(NewsPaths::TMP_FEED, indexTmp, bodyTmp, keepOrdinals_, keepOrdinals_[0], localOffsetSec_)) {
    Storage.remove(NewsPaths::TMP_FEED);
    Storage.remove(indexTmp);
    Storage.remove(bodyTmp);
    return -1;
  }

  bool failed = false;
  for (int steps = 0;; steps++) {
    const auto status = parser.step();
    if (status == NewsFeedParser::Step::Error) {
      failed = true;
      break;
    }
    if (status == NewsFeedParser::Step::Done) break;
    if (steps % STEPS_PER_REPAINT == STEPS_PER_REPAINT - 1) requestUpdate();
  }

  NewsFeedParser::Result result;
  if (failed || !parser.finish(result)) {
    parser.abort();
    Storage.remove(NewsPaths::TMP_FEED);
    Storage.remove(indexTmp);
    Storage.remove(bodyTmp);
    return -1;
  }

  LOG_INF("NEWS", "slot %zu kept %u of %u free=%u largest=%u", slot, result.itemsKept, result.itemsSeen,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // A user feed's real name only becomes known once its <title> is parsed.
  if (slot >= NEWS_FEEDS.builtinCount() && parser.feedTitle()[0] != '\0') {
    NEWS_FEEDS.setUserFeedName(slot - NEWS_FEEDS.builtinCount(), parser.feedTitle());
  }

  Storage.remove(NewsPaths::TMP_FEED);
  // Carry forward the items an earlier sync stored: most feeds only publish a
  // day or two, so without this the older tabs could never fill up.
  const uint16_t total = mergePreviousItems(slot, indexTmp, bodyTmp, result.itemsKept);

  Storage.remove(indexPath);
  Storage.remove(bodyPath);
  if (!Storage.rename(indexTmp, indexPath) || !Storage.rename(bodyTmp, bodyPath)) {
    LOG_ERR("NEWS", "slot %zu rename failed", slot);
    Storage.remove(indexTmp);
    Storage.remove(bodyTmp);
    return -1;
  }
  return total;
}

void NewsSyncActivity::syncAll() {
  // HTTPS needs a plausible clock, and so does deciding which day an item
  // belongs to. A failed sync leaves the ordinals zeroed, which the parser
  // reads as "keep everything" rather than dropping the whole download.
  if (!HalClock::isSystemTimeValid()) {
    HalClock::quickSyncSystemTime();
  }
  localOffsetSec_ = TimeUtils::localOffsetSeconds();
  const uint32_t today = TimeUtils::todayOrdinal();
  for (size_t i = 0; i < NewsIndex::DAY_COUNT; i++) {
    keepOrdinals_[i] = (today > i) ? static_cast<uint32_t>(today - i) : 0;
  }

  const size_t slots = NEWS_FEEDS.slotCount();
  pruneCache(slots);

  int enabled = 0;
  for (size_t slot = 0; slot < slots; slot++) {
    if (NEWS_FEEDS.slotEnabled(slot)) enabled++;
  }
  {
    RenderLock lock(*this);
    totalFeeds_ = enabled;
  }

  int index = 0;
  int itemsTotal = 0;
  int feedsOk = 0;
  for (size_t slot = 0; slot < slots; slot++) {
    if (!NEWS_FEEDS.slotEnabled(slot)) continue;
    index++;
    {
      RenderLock lock(*this);
      currentFeed_ = index;
      snprintf(currentFeedName_, sizeof(currentFeedName_), "%s", NEWS_FEEDS.slotName(slot));
    }
    requestUpdateAndWait();

    const int kept = syncOneFeed(slot);
    // One bad feed must not cost the user the rest of the sync.
    if (kept >= 0) {
      feedsOk++;
      itemsTotal += kept;
    }
  }

  if (feedsOk > 0) {
    NEWS_FEEDS.setLastSyncEpoch(static_cast<uint32_t>(time(nullptr)));
    NEWS_FEEDS.saveToFile();
  }

  // The Wi-Fi is already up and the items are freshly indexed, which is the
  // cheapest moment to collect the articles the reader asked for.
  fetchQueuedArticles(slots);

  RenderLock lock(*this);
  itemsTotal_ = itemsTotal;
  feedsOk_ = feedsOk;
  state_ = (feedsOk > 0) ? COMPLETE : ERROR;
}

void NewsSyncActivity::fetchQueuedArticles(const size_t slots) {
  // Collected first, then fetched: the fetch rewrites the very records this
  // walk is reading, and each page costs 130KB to 440KB, so the run is capped.
  struct Queued {
    uint8_t slot;
    uint16_t item;
  };
  Queued queue[MAX_QUEUED_FETCHES];
  int queued = 0;

  for (size_t slot = 0; slot < slots && queued < static_cast<int>(MAX_QUEUED_FETCHES); slot++) {
    if (!NEWS_FEEDS.slotEnabled(slot) || !NEWS_FEEDS.fullTextOffered(slot)) continue;
    const uint16_t count = NewsIndexIo::itemCount(slot);
    for (uint16_t item = 0; item < count && queued < static_cast<int>(MAX_QUEUED_FETCHES); item++) {
      NewsIndex::Record record{};
      if (!NewsIndexIo::readRecord(slot, item, record)) break;
      if ((record.flags & NewsIndex::FLAG_QUEUED) == 0 || record.fullLen > 0) continue;
      queue[queued++] = Queued{static_cast<uint8_t>(slot), item};
    }
  }
  if (queued == 0) return;

  {
    RenderLock lock(*this);
    articlesTotal_ = queued;
    articlesDone_ = 0;
  }
  for (int i = 0; i < queued; i++) {
    {
      RenderLock lock(*this);
      articlesDone_ = i + 1;
    }
    requestUpdateAndWait();
    // A site that refuses the device fails fast; one that does not is worth the
    // wait. Either way the next queued item still gets its turn.
    NewsFullTextActivity::fetchInto(queue[i].slot, queue[i].item);
  }
}

void NewsSyncActivity::loop() {
  if (state_ == COMPLETE || state_ == ERROR) {
    // Release, not press: the screen this returns to acts on the release, and
    // would otherwise take the same button event as its own dismissal.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}

void NewsSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NEWS_SYNC));

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int centerY = (pageHeight - lineHeight) / 2;
  char buf[96];

  if (state_ == SYNCING) {
    if (articlesTotal_ > 0) {
      snprintf(buf, sizeof(buf), tr(STR_NEWS_FETCHING_FULL_N), articlesDone_, articlesTotal_);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, buf, true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + lineHeight / 2, tr(STR_NEWS_FETCHING_FULL));
    } else if (totalFeeds_ > 0) {
      snprintf(buf, sizeof(buf), tr(STR_NEWS_SYNCING_FEED), currentFeed_, totalFeeds_);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, buf, true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + lineHeight / 2, currentFeedName_);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_LOADING));
    }
  } else if (state_ == COMPLETE) {
    snprintf(buf, sizeof(buf), "%s (%d)", tr(STR_NEWS_SYNC_DONE), itemsTotal_);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, buf, true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_NEWS_SYNC_FAILED), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
