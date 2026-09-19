#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <iterator>

void RecentBooksStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : recentBooks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    }
  JsonArray hidden = doc["hidden"].to<JsonArray>();
  for (const auto& path : hiddenPaths) hidden.add(path);
}

bool RecentBooksStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'books' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  recentBooks.clear();
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  recentBooks.reserve(std::min(arr.size(), static_cast<size_t>(MAX_RECENT_BOOKS)));
  for (JsonObjectConst obj : arr) {
    if (getCount() >= MAX_RECENT_BOOKS) break;
    RecentBook book;
    book.path = obj["path"] | "";
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.coverBmpPath = obj["coverBmpPath"] | "";
      recentBooks.push_back(book);
  }

  // Absent in files written before Hide from Recents existed; a null array
  // iterates zero times, which is the right answer for those.
  hiddenPaths.clear();
  for (JsonVariantConst entry : doc["hidden"].as<JsonArrayConst>()) {
    if (hiddenPaths.size() >= MAX_HIDDEN_BOOKS) break;
    const std::string path = entry.as<const char*>() ? entry.as<const char*>() : "";
    if (!path.empty()) hiddenPaths.push_back(path);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries, %u hidden)", getCount(), (unsigned)hiddenPaths.size());
  return true;
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath) {
  // Drop stale entries first so a new add can't evict a valid book in their stead.
  pruneMissing();

  // Opening a book unhides it -- the one way back from Hide from Recents, and the
  // reason hiding is offered without a confirmation. Every route into a reader
  // lands here (file browser, OPDS download, resume), so this is the single place
  // it has to be undone.
  hiddenPaths.erase(std::remove(hiddenPaths.begin(), hiddenPaths.end(), path), hiddenPaths.end());

  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
  }

  // Add to front
  RecentBook book;
  book.path = path;
  book.title = title;
  book.author = author;
  book.coverBmpPath = coverBmpPath;
  recentBooks.insert(recentBooks.begin(), std::move(book));

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

void RecentBooksStore::setHiddenFromRecents(const std::string& path, const bool hidden) {
  if (path.empty()) return;
  const auto it = std::find(hiddenPaths.begin(), hiddenPaths.end(), path);
  if (hidden) {
    if (it != hiddenPaths.end()) return;
    if (hiddenPaths.size() >= MAX_HIDDEN_BOOKS) {
      LOG_ERR("RBS", "Hidden-book list full, not hiding: %s", path.c_str());
      return;
    }
    hiddenPaths.push_back(path);
    // Not removeByPath(): that saves too, and the book is usually open right now,
    // so this is one write rather than two.
    recentBooks.erase(
        std::remove_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& b) { return b.path == path; }),
        recentBooks.end());
  } else {
    if (it == hiddenPaths.end()) return;
    hiddenPaths.erase(it);
    // No recents entry is restored: hiding threw it away. The card scan finds the
    // file again, which is the same state a never-opened book is in.
  }
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to persist hidden state for: %s", path.c_str());
  }
}

bool RecentBooksStore::isHiddenFromRecents(const std::string& path) const {
  return std::find(hiddenPaths.begin(), hiddenPaths.end(), path) != hiddenPaths.end();
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  recentBooks.erase(it);
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
  }
  return true;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });
  if (it == recentBooks.end()) {
    return;
  }
  it->path = newPath;
  if (!oldCachePath.empty() && !it->coverBmpPath.empty() && it->coverBmpPath.rfind(oldCachePath, 0) == 0) {
    it->coverBmpPath = newCachePath + it->coverBmpPath.substr(oldCachePath.size());
  }
  saveToFile();
}

bool RecentBooksStore::isMissing(const RecentBook& book) {
  // Only real book files count as present: the formats a reader activity can
  // actually open (.epub/.xtc/.xtch/.txt/.md). Corrupt or legacy entries (empty
  // paths, non-book paths, folders that merely exist) must not survive pruning --
  // they inflated the home screen's "+N" stack badge with entries that are not
  // books.
  if (book.path.empty() || !(FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path) ||
                             FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path))) {
    return true;
  }
  return !Storage.exists(book.path.c_str());
}

bool RecentBooksStore::pruneMissing() {
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &isMissing), recentBooks.end());
  return recentBooks.size() != before;
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover.
  // Use buildIfMissing=false to avoid heavy epub loading on boot; getTitle()/getAuthor() may be
  // blank until the book is opened, and entries with missing title are omitted from recent list.
  if (FsHelpers::hasEpubExtension(lastBookFileName)) {
    Epub epub(path, "/.crosspoint");
    epub.load(false, true);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(), epub.getThumbBmpPath()};
  } else if (FsHelpers::hasXtcExtension(lastBookFileName)) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{path, xtc.getTitle(), xtc.getAuthor(), xtc.getThumbBmpPath()};
    }
  } else if (FsHelpers::hasTxtExtension(lastBookFileName) || FsHelpers::hasMarkdownExtension(lastBookFileName)) {
    return RecentBook{path, lastBookFileName, "", ""};
  }
  return RecentBook{path, "", "", ""};
}
