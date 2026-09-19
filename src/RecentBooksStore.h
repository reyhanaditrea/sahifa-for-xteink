#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

class RecentBooksStore : public PersistableStore<RecentBooksStore> {
 private:
  std::vector<RecentBook> recentBooks;

  // Books the user hid from My Books (reader menu -> Hide from Recents). Paths
  // only, and a separate list rather than a flag on RecentBook: a hidden book
  // has to outlive the MAX_RECENT_BOOKS trim below, or hiding it and then
  // opening ten other books would evict the record and the card scan would put
  // it straight back on the screen.
  std::vector<std::string> hiddenPaths;

  static constexpr int MAX_RECENT_BOOKS = 10;
  // Bounds the file. Can't realistically be reached: the My Books scan itself
  // stops at MAX_LIBRARY_BOOKS (200), so there are never more than that many
  // books on screen to hide in the first place.
  static constexpr size_t MAX_HIDDEN_BOOKS = 200;

  RecentBooksStore() = default;
  ~RecentBooksStore() = default;

  friend class PersistableStore<RecentBooksStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/recent.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Add a book to the recent list (moves to front if already exists).
  void addBook(const std::string& path, const std::string& title, const std::string& author,
               const std::string& coverBmpPath);

  void updateBook(const std::string& path, const std::string& title, const std::string& author,
                  const std::string& coverBmpPath);

  // Hide a book from My Books, or put it back. Hiding drops the recents entry
  // (same as removeByPath -- the stored title/author/progress metadata goes with
  // it) and remembers the path so the card scan doesn't rediscover the file and
  // list it again as a never-opened book, which is what made the plain removal
  // look like it had done nothing. Opening the book again clears it: addBook()
  // unhides, so the file browser is always a way back and the action never needs
  // a confirmation prompt. Persists.
  void setHiddenFromRecents(const std::string& path, bool hidden);
  bool isHiddenFromRecents(const std::string& path) const;

  // Remove the entry whose path matches (used when a book is removed from recents or finished/read).
  // Returns true if an entry was found and removed (no-op + false otherwise).
  // Persistence is best-effort: a failed save is logged, not reflected in the return.
  bool removeByPath(const std::string& path);

  // Repoint an entry's path (and coverBmpPath, if it lived under the old cache dir) after the
  // backing file and cache dir were moved on disk. No-op if no entry matches oldPath.
  // Persists on success. Keeps the entry's list position (does not reorder).
  void updatePath(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                  const std::string& newCachePath);

  // True if the book's backing file is no longer present on the SD card.
  static bool isMissing(const RecentBook& book);

  // Remove entries whose backing file is no longer on the SD card.
  // Returns true if any entry was removed. Does not persist — caller decides.
  bool pruneMissing();

  // Get the list of recent books (most recent first)
  const std::vector<RecentBook>& getBooks() const { return recentBooks; }

  // Get the count of recent books
  int getCount() const { return static_cast<int>(recentBooks.size()); }

  RecentBook getDataFromBook(std::string path) const;
};

// Helper macro to access recent books store
#define RECENT_BOOKS RecentBooksStore::getInstance()
