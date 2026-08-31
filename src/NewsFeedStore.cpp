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
  JsonArray order = doc["builtinOrder"].to<JsonArray>();
  for (size_t i = 0; i < NewsBuiltins::COUNT; i++) {
    order.add(builtinOrder_[i]);
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
    obj["enabled"] = userFeeds_[i].enabled != 0;
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

  // Rebuilt rather than trusted: a hand-edited file, or an outlet a later
  // firmware added, must still leave a permutation. Anything unusable is
  // dropped and whatever the stored order left out is appended in table order,
  // so a new outlet joins at the end instead of displacing the user's layout.
  bool seen[NewsBuiltins::COUNT] = {};
  size_t orderCount = 0;
  for (const JsonVariantConst value : doc["builtinOrder"].as<JsonArrayConst>()) {
    const size_t index = value | static_cast<size_t>(NewsBuiltins::COUNT);
    if (index >= NewsBuiltins::COUNT || seen[index]) continue;
    seen[index] = true;
    builtinOrder_[orderCount++] = static_cast<uint8_t>(index);
  }
  for (size_t i = 0; i < NewsBuiltins::COUNT; i++) {
    if (!seen[i]) builtinOrder_[orderCount++] = static_cast<uint8_t>(i);
  }

  userCount_ = 0;
  for (JsonObjectConst obj : doc["feeds"].as<JsonArrayConst>()) {
    if (userCount_ >= MAX_USER_FEEDS) break;
    const char* url = obj["url"] | "";
    if (url[0] == '\0') continue;
    copyBounded(userFeeds_[userCount_].url, sizeof(userFeeds_[0].url), url);
    copyBounded(userFeeds_[userCount_].name, sizeof(userFeeds_[0].name), obj["name"] | url);
    userFeeds_[userCount_].fullText = obj["fullText"] | static_cast<uint8_t>(0);
    userFeeds_[userCount_].enabled = (obj["enabled"] | true) ? 1 : 0;
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

void NewsFeedStore::resetBuiltinOrder() {
  for (size_t i = 0; i < NewsBuiltins::COUNT; i++) {
    builtinOrder_[i] = static_cast<uint8_t>(i);
  }
}

size_t NewsFeedStore::builtinAt(const size_t position) const {
  if (position >= NewsBuiltins::COUNT) return 0;
  return builtinOrder_[position];
}

bool NewsFeedStore::moveBuiltin(const size_t position, const int delta) {
  if (position >= NewsBuiltins::COUNT || (delta != 1 && delta != -1)) return false;
  const size_t target = static_cast<size_t>(static_cast<int>(position) + delta);
  if (target >= NewsBuiltins::COUNT) return false;
  const uint8_t moved = builtinOrder_[position];
  builtinOrder_[position] = builtinOrder_[target];
  builtinOrder_[target] = moved;
  return saveToFile();
}

bool NewsFeedStore::moveUserFeed(const size_t index, const int delta) {
  if (index >= userCount_ || (delta != 1 && delta != -1)) return false;
  const size_t target = static_cast<size_t>(static_cast<int>(index) + delta);
  if (target >= userCount_) return false;
  const NewsUserFeed moved = userFeeds_[index];
  userFeeds_[index] = userFeeds_[target];
  userFeeds_[target] = moved;
  return saveToFile();
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
  userFeeds_[userCount_].fullText = static_cast<uint8_t>(FullText::Unknown);
  userFeeds_[userCount_].enabled = 1;
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

bool NewsFeedStore::userFeedEnabled(const size_t index) const {
  if (index >= userCount_) return false;
  return userFeeds_[index].enabled != 0;
}

void NewsFeedStore::setUserFeedEnabled(const size_t index, const bool enabled) {
  if (index >= userCount_) return;
  userFeeds_[index].enabled = enabled ? 1 : 0;
  // Same reasoning as the built-ins: switching a feed back on is the user
  // asking for its pages to be tried again.
  userFeeds_[index].fullText = static_cast<uint8_t>(FullText::Unknown);
}

void NewsFeedStore::setUserFeedName(const size_t index, const char* name) {
  if (index >= userCount_ || !name || name[0] == '\0') return;
  copyBounded(userFeeds_[index].name, sizeof(userFeeds_[0].name), name);
}

const char* NewsFeedStore::slotName(const size_t slot) const {
  if (slot < NewsBuiltins::COUNT) return NewsBuiltins::FEEDS[builtinAt(slot)].name;
  const size_t userIndex = slot - NewsBuiltins::COUNT;
  if (userIndex >= userCount_) return "";
  return userFeeds_[userIndex].name;
}

const char* NewsFeedStore::slotUrl(const size_t slot) const {
  if (slot < NewsBuiltins::COUNT) return NewsBuiltins::FEEDS[builtinAt(slot)].url;
  const size_t userIndex = slot - NewsBuiltins::COUNT;
  if (userIndex >= userCount_) return "";
  return userFeeds_[userIndex].url;
}

bool NewsFeedStore::slotEnabled(const size_t slot) const {
  if (slot < NewsBuiltins::COUNT) return builtinEnabled(builtinAt(slot));
  return userFeedEnabled(slot - NewsBuiltins::COUNT);
}

size_t NewsFeedStore::enabledSlotCount() const {
  size_t count = enabledBuiltinCount();
  for (uint8_t i = 0; i < userCount_; i++) {
    if (userFeeds_[i].enabled != 0) count++;
  }
  return count;
}

NewsFeedStore::FullText NewsFeedStore::fullTextState(const size_t slot) const {
  if (slot < NewsBuiltins::COUNT) return static_cast<FullText>(builtinFullText_[builtinAt(slot)]);
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
    builtinFullText_[builtinAt(slot)] = static_cast<uint8_t>(next);
  } else {
    const size_t userIndex = slot - NewsBuiltins::COUNT;
    if (userIndex >= userCount_) return;
    userFeeds_[userIndex].fullText = static_cast<uint8_t>(next);
  }
  saveToFile();
}

void NewsFeedStore::clearFullTextState(const size_t slot) {
  if (slot < NewsBuiltins::COUNT) {
    builtinFullText_[builtinAt(slot)] = static_cast<uint8_t>(FullText::Unknown);
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
