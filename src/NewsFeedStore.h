#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>

#include "news/NewsBuiltins.h"

// Feeds the News app syncs from: the built-in outlets (toggled on and off, kept
// in flash) plus whatever the user added on the device keyboard.
//
// Everything is fixed-size char[] rather than std::string: the store is alive
// for the life of the app and a handful of small heap blocks would fragment the
// DRAM the parser needs.
struct NewsUserFeed {
  char name[32];
  char url[128];
  uint8_t fullText;  // NewsFeedStore::FullText, as a byte so the struct is POD
};

class NewsFeedStore : public PersistableStore<NewsFeedStore> {
 public:
  static constexpr size_t MAX_USER_FEEDS = 8;
  // Built-ins first, then user feeds -- the order the sync and the UI walk.
  static constexpr size_t MAX_SLOTS = NewsBuiltins::COUNT + MAX_USER_FEEDS;

  static const char* getFilePath() { return "/.crosspoint/news_feeds.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // --- built-in outlets ---
  static size_t builtinCount() { return NewsBuiltins::COUNT; }
  bool builtinEnabled(size_t index) const;
  void setBuiltinEnabled(size_t index, bool enabled);
  size_t enabledBuiltinCount() const;

  // --- user feeds ---
  size_t userCount() const { return userCount_; }
  const NewsUserFeed* userFeed(size_t index) const;
  // False when the list is full or the URL is already present.
  bool addUserFeed(const char* name, const char* url);
  bool removeUserFeed(size_t index);
  bool hasUrl(const char* url) const;
  // Replaces a user feed's display name once a sync has read the feed's own
  // <title>; the host name is only a placeholder until then.
  void setUserFeedName(size_t index, const char* name);

  // --- unified slot view (built-ins then user feeds) ---
  // Slots are stable while the store is unchanged; feedN.idx on SD is keyed by
  // slot, so adding or removing a user feed invalidates the cache.
  size_t slotCount() const { return NewsBuiltins::COUNT + userCount_; }
  const char* slotName(size_t slot) const;
  const char* slotUrl(size_t slot) const;
  bool slotEnabled(size_t slot) const;
  size_t enabledSlotCount() const;

  uint32_t lastSyncEpoch() const { return lastSyncEpoch_; }
  void setLastSyncEpoch(const uint32_t epoch) { lastSyncEpoch_ = epoch; }

  // --- full-text fetching, learned per feed ---
  // Whether the outlet's own pages can be read. Nothing predicts this from the
  // feed alone -- a site answers 403 to the device, hides its story behind a
  // consent wall, or renders it in JavaScript -- so the answer comes from
  // actually trying, once, and is remembered for the whole outlet.
  enum class FullText : uint8_t {
    Unknown = 0,  // never tried
    Ok = 1,       // a fetch worked
    Failed = 2,   // one fetch came back empty
    Blocked = 3,  // two in a row: stop offering it
  };
  FullText fullTextState(size_t slot) const;
  // True while the option is still worth showing to the user.
  bool fullTextOffered(size_t slot) const { return fullTextState(slot) != FullText::Blocked; }
  // Records the outcome of a fetch: success clears the history, a failure
  // advances Unknown/Ok -> Failed -> Blocked.
  void noteFullTextResult(size_t slot, bool ok);
  void clearFullTextState(size_t slot);

  // The article to reopen after the silent restart a Wi-Fi fetch ends with.
  // RTC only carries the reboot target, so the coordinates live here.
  bool pendingArticle(size_t& slot, uint16_t& item) const;
  void setPendingArticle(size_t slot, uint16_t item);
  void clearPendingArticle();

 private:
  NewsFeedStore() = default;
  friend class PersistableStore<NewsFeedStore>;

  NewsUserFeed userFeeds_[MAX_USER_FEEDS] = {};
  uint8_t userCount_ = 0;
  // Bit i set => built-in i is off. Defaulting to "all bits clear" means a new
  // outlet in a later firmware arrives enabled.
  uint16_t builtinsDisabled_ = 0;
  uint32_t lastSyncEpoch_ = 0;
  // Keyed by the built-in's index in the flash table, so it survives the user
  // adding or removing feeds of their own.
  uint8_t builtinFullText_[NewsBuiltins::COUNT] = {};
  static constexpr uint16_t NO_PENDING = 0xFFFF;
  uint16_t pendingSlot_ = NO_PENDING;
  uint16_t pendingItem_ = 0;
};

#define NEWS_FEEDS NewsFeedStore::getInstance()
