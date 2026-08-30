#include "NewsFeedStore.h"

#include <Logging.h>

#include <cstring>

namespace {

// strlcpy is not universally available on the host toolchains the pure tests
// build with, and this is the only place the store copies into fixed buffers.
void copyBounded(char* dst, const size_t dstLen, const char* src) {
  if (dstLen == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t i = 0;
  while (i + 1 < dstLen && src[i] != '\0') {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

}  // namespace

void NewsFeedStore::toJson(JsonDocument& doc) const {
  doc["builtinsDisabled"] = builtinsDisabled_;
  doc["lastSync"] = lastSyncEpoch_;
  JsonArray full = doc["builtinFullText"].to<JsonArray>();
  for (size_t i = 0; i < NewsBuiltins::COUNT; i++) {
    full.add(builtinFullText_[i]);
  }
  if (pendingSlot_ != NO_PENDING) {
    doc["openSlot"] = pendingSlot_;
    doc["openItem"] = pendingItem_;
  }
  if (fullTextPending_) doc["fetchFull"] = true;
  JsonArray arr = doc["feeds"].to<JsonArray>();
  for (uint8_t i = 0; i < userCount_; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = userFeeds_[i].name;
    obj["url"] = userFeeds_[i].url;
    obj["fullText"] = userFeeds_[i].fullText;
  }
}

bool NewsFeedStore::fromJson(JsonVariantConst doc) {
  builtinsDisabled_ = doc["builtinsDisabled"] | static_cast<uint16_t>(0);
  lastSyncEpoch_ = doc["lastSync"] | static_cast<uint32_t>(0);
  pendingSlot_ = doc["openSlot"] | NO_PENDING;
  pendingItem_ = doc["openItem"] | static_cast<uint16_t>(0);
  fullTextPending_ = doc["fetchFull"] | false;

  size_t builtinIndex = 0;
  for (const JsonVariantConst value : doc["builtinFullText"].as<JsonArrayConst>()) {
    if (builtinIndex >= NewsBuiltins::COUNT) break;
    builtinFullText_[builtinIndex++] = value | static_cast<uint8_t>(0);
  }
  for (; builtinIndex < NewsBuiltins::COUNT; builtinIndex++) {
    builtinFullText_[builtinIndex] = 0;
  }

  userCount_ = 0;
  for (JsonObjectConst obj : doc["feeds"].as<JsonArrayConst>()) {
    if (userCount_ >= MAX_USER_FEEDS) break;
    const char* url = obj["url"] | "";
    if (url[0] == '\0') continue;
    copyBounded(userFeeds_[userCount_].url, sizeof(userFeeds_[0].url), url);
    copyBounded(userFeeds_[userCount_].name, sizeof(userFeeds_[0].name), obj["name"] | url);
    userFeeds_[userCount_].fullText = obj["fullText"] | static_cast<uint8_t>(0);
    userCount_++;
  }
  LOG_DBG("NEWS", "Loaded %u user feeds, builtins disabled mask 0x%04X", userCount_, builtinsDisabled_);
  return true;
}

bool NewsFeedStore::builtinEnabled(const size_t index) const {
  if (index >= NewsBuiltins::COUNT) return false;
  return (builtinsDisabled_ & (1U << index)) == 0;
}

void NewsFeedStore::setBuiltinEnabled(const size_t index, const bool enabled) {
  if (index >= NewsBuiltins::COUNT) return;
  // Switching an outlet back on is the user's way of saying "try again": the
  // site may have changed, or the block may have been a bad day on the network.
  builtinFullText_[index] = static_cast<uint8_t>(FullText::Unknown);
  const uint16_t bit = static_cast<uint16_t>(1U << index);
  if (enabled) {
    builtinsDisabled_ &= static_cast<uint16_t>(~bit);
  } else {
    builtinsDisabled_ |= bit;
  }
}

size_t NewsFeedStore::enabledBuiltinCount() const {
  size_t count = 0;
  for (size_t i = 0; i < NewsBuiltins::COUNT; i++) {
    if (builtinEnabled(i)) count++;
  }
  return count;
}

const NewsUserFeed* NewsFeedStore::userFeed(const size_t index) const {
  if (index >= userCount_) return nullptr;
  return &userFeeds_[index];
}

bool NewsFeedStore::hasUrl(const char* url) const {
  if (!url) return false;
  for (size_t i = 0; i < NewsBuiltins::COUNT; i++) {
    if (strcmp(NewsBuiltins::FEEDS[i].url, url) == 0) return true;
  }
  for (uint8_t i = 0; i < userCount_; i++) {
    if (strcmp(userFeeds_[i].url, url) == 0) return true;
  }
  return false;
}

bool NewsFeedStore::addUserFeed(const char* name, const char* url) {
  if (!url || url[0] == '\0') return false;
  if (userCount_ >= MAX_USER_FEEDS) {
    LOG_DBG("NEWS", "User feed list full (%zu)", MAX_USER_FEEDS);
    return false;
  }
  if (hasUrl(url)) {
    LOG_DBG("NEWS", "Feed already present: %s", url);
    return false;
  }
  copyBounded(userFeeds_[userCount_].url, sizeof(userFeeds_[0].url), url);
  copyBounded(userFeeds_[userCount_].name, sizeof(userFeeds_[0].name), (name && name[0]) ? name : url);
  userCount_++;
  return saveToFile();
}

bool NewsFeedStore::removeUserFeed(const size_t index) {
  if (index >= userCount_) return false;
  for (size_t i = index; i + 1 < userCount_; i++) {
    userFeeds_[i] = userFeeds_[i + 1];
  }
  userCount_--;
  userFeeds_[userCount_] = NewsUserFeed{};
  return saveToFile();
}

void NewsFeedStore::setUserFeedName(const size_t index, const char* name) {
  if (index >= userCount_ || !name || name[0] == '\0') return;
  copyBounded(userFeeds_[index].name, sizeof(userFeeds_[0].name), name);
}

const char* NewsFeedStore::slotName(const size_t slot) const {
  if (slot < NewsBuiltins::COUNT) return NewsBuiltins::FEEDS[slot].name;
  const size_t userIndex = slot - NewsBuiltins::COUNT;
  if (userIndex >= userCount_) return "";
  return userFeeds_[userIndex].name;
}

const char* NewsFeedStore::slotUrl(const size_t slot) const {
  if (slot < NewsBuiltins::COUNT) return NewsBuiltins::FEEDS[slot].url;
  const size_t userIndex = slot - NewsBuiltins::COUNT;
  if (userIndex >= userCount_) return "";
  return userFeeds_[userIndex].url;
}

bool NewsFeedStore::slotEnabled(const size_t slot) const {
  if (slot < NewsBuiltins::COUNT) return builtinEnabled(slot);
  return slot - NewsBuiltins::COUNT < userCount_;
}

size_t NewsFeedStore::enabledSlotCount() const { return enabledBuiltinCount() + userCount_; }

NewsFeedStore::FullText NewsFeedStore::fullTextState(const size_t slot) const {
  if (slot < NewsBuiltins::COUNT) return static_cast<FullText>(builtinFullText_[slot]);
  const size_t userIndex = slot - NewsBuiltins::COUNT;
  if (userIndex >= userCount_) return FullText::Unknown;
  return static_cast<FullText>(userFeeds_[userIndex].fullText);
}

void NewsFeedStore::noteFullTextResult(const size_t slot, const bool ok) {
  const FullText previous = fullTextState(slot);
  FullText next = FullText::Ok;
  if (!ok) {
    // A feed that worked before gets a second chance before it is written off.
    next = (previous == FullText::Failed || previous == FullText::Blocked) ? FullText::Blocked : FullText::Failed;
  }
  if (next == previous) return;
  if (slot < NewsBuiltins::COUNT) {
    builtinFullText_[slot] = static_cast<uint8_t>(next);
  } else {
    const size_t userIndex = slot - NewsBuiltins::COUNT;
    if (userIndex >= userCount_) return;
    userFeeds_[userIndex].fullText = static_cast<uint8_t>(next);
  }
  saveToFile();
}

void NewsFeedStore::clearFullTextState(const size_t slot) {
  if (slot < NewsBuiltins::COUNT) {
    builtinFullText_[slot] = static_cast<uint8_t>(FullText::Unknown);
    saveToFile();
    return;
  }
  const size_t userIndex = slot - NewsBuiltins::COUNT;
  if (userIndex >= userCount_) return;
  userFeeds_[userIndex].fullText = static_cast<uint8_t>(FullText::Unknown);
  // Persisted here rather than left to the caller: the retry that clears this
  // ends in a restart, and an unsaved reset would come back Blocked.
  saveToFile();
}

bool NewsFeedStore::pendingArticle(size_t& slot, uint16_t& item) const {
  if (pendingSlot_ == NO_PENDING) return false;
  slot = pendingSlot_;
  item = pendingItem_;
  return true;
}

void NewsFeedStore::setPendingArticle(const size_t slot, const uint16_t item) {
  pendingSlot_ = static_cast<uint16_t>(slot);
  pendingItem_ = item;
  saveToFile();
}

void NewsFeedStore::clearPendingArticle() {
  if (pendingSlot_ == NO_PENDING) return;
  pendingSlot_ = NO_PENDING;
  pendingItem_ = 0;
  saveToFile();
}

void NewsFeedStore::setFullTextPending(const bool pending) {
  if (fullTextPending_ == pending) return;
  fullTextPending_ = pending;
  // Written through immediately: the flag exists to survive the reboot that
  // happens moments later, and it is cleared before the fetch starts so a
  // crash inside the fetch cannot come back to the same fetch forever.
  saveToFile();
}
