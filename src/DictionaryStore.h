#pragma once

#include <StarDictIndex.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct DictionaryEntry {
  std::string languageId;
  std::string directoryPath;
  std::string ifoPath;
  std::string idxPath;
  std::string dictPath;
  std::string synPath;
  std::string cachePath;
  std::string name;
  std::string lang;
  std::string sameTypeSequence;
  uint32_t wordCount = 0;
  uint32_t idxFileSize = 0;
  uint32_t synFileSize = 0;
  bool compressed = false;
  bool missingFiles = false;
  // .ifo declared idxoffsetbits=64 (12-byte index records instead of 8). Every
  // index reader here hardcodes the 8-byte layout, so such a dictionary must be
  // refused outright rather than silently parsed into garbage offsets.
  bool unsupportedFormat = false;
  // Built on first use and cached on SD next to the dictionary. The .syn table
  // is empty when the dictionary ships no synonym file.
  mutable StarDictIndex::CheckpointTable idxTable;
  mutable StarDictIndex::CheckpointTable synTable;
};

// One reading of the word that reached an entry of its own.
//
// The stemmer's candidate order is a ranking, and lookup() takes the first
// candidate that hits -- but three letters can be two different words and the
// marks do not always say which, so the reader is shown the others and can open
// one. Each carries where its article lives, so opening it is a read rather than
// a second lookup.
struct DictionaryReading {
  std::string headword;
  // "عرض + هم", or empty when this reading is the word as written.
  std::string decomposition;
  // What kind of evidence this reading rests on: ArabicStem::standingLabel.
  std::string standing;
  uint32_t dictOffset = 0;
  uint32_t dictSize = 0;
};

struct DictionaryLookupResult {
  // A headword was located in the index but its definition could not be
  // read back -- distinct from NotFound (the word genuinely isn't in the
  // dictionary) so the UI never reports a real hit as a silent miss.
  enum class Status {
    Found,
    NotFound,
    NoDictionary,
    NotReady,
    LowMemory,        // fragmented-heap allocation failure reading/decompressing
    DecompressError,  // .dict.dz stream was corrupt/truncated
    ReadError,        // SD open/seek/read failure, or a bogus .idx offset
  };

  Status status = Status::NotFound;
  std::string query;
  std::string headword;
  // How the word was cut up to reach the headword: "عرض + هم". Empty when the
  // query was found as written, or when nothing came off it. The same string
  // tools/dictprobe prints and the browser simulator shows -- both call
  // ArabicStem::segments on the candidate that answered.
  std::string decomposition;
  std::string definition;
  // The one plain sentence the article can be reduced to, for layer 1. Empty when
  // nothing in it reads as a meaning.
  std::string gloss;
  // Every reading that reached an entry, the answering one first. At most
  // MAX_READINGS; empty when the word was found as written and nothing else was.
  std::vector<DictionaryReading> readings;
  std::string dictionaryName;
  bool truncated = false;
  std::vector<std::string> suggestions;
};

class DictionaryStore {
 public:
  // Public: the download store (DictionaryDownloadActivity) installs sets here.
  static constexpr const char* DICTIONARY_ROOT = "/dictionaries";
  // DEF_TEXT_READER is a font *family* choice rather than a third size: it renders
  // definitions in the book's own reading font instead of the fixed UI tiers, so an
  // Arabic definition stops arriving in the Tajawal UI face while the page behind it
  // is set in Noto Naskh (or whichever Arabic reading font is selected). No config
  // version bump is needed -- the accepted range only widens, and no value written by
  // an older build can land on it. See getDefinitionFontId().
  enum DefinitionTextSize : uint8_t {
    DEF_TEXT_SMALL = 0,
    DEF_TEXT_LARGE = 1,
    DEF_TEXT_READER = 2,
    DEF_TEXT_SIZE_COUNT
  };
  // Kept next to the enum because these two drift silently otherwise: an option
  // added to the enum but missing from a caller's own label list simply cannot be
  // picked, and the value column reports the wrong one for it -- which is exactly
  // what happened to DEF_TEXT_READER in the reader drawer. Any list of these labels
  // should come from here rather than be spelled out again.
  static const char* definitionTextSizeLabel(uint8_t size);

  static DictionaryStore& getInstance();

  void loadConfig();
  bool saveConfig() const;
  void scan();
  void ensureScanned();
  const std::vector<DictionaryEntry>& getEntries() const { return entries; }
  // Gates the reader menu's Look up word / Lookup history rows so they only
  // appear once a dictionary is actually installed. Scans on first call so
  // the row is right the first time the drawer opens, without requiring a
  // prior visit to the Dictionary app.
  bool hasAnyDictionary() {
    ensureScanned();
    return !entries.empty();
  }
  int getActiveIndex() const { return activeIndex; }
  bool setActiveIndex(int index);
  // Called when a set is deleted so a since-removed dictionary can't linger
  // as the persisted active selection (scan() already refuses to resurrect
  // it, but this also keeps dictionary_config.json itself from pointing at
  // a path that no longer exists).
  void clearActiveIfMatches(const std::string& ifoPath);
  std::string getActiveLabel() const;
  uint8_t getDefinitionTextSize() const { return definitionTextSize; }
  bool setDefinitionTextSize(uint8_t size);
  int getDefinitionFontId(int readerFontId) const;
  bool prepareActive(const std::function<void(int percent)>& onProgress = nullptr);
  bool hasActiveDictionary() const;

  // `previousWord` is the token in front of this one on the page, unedited. It is
  // evidence, not context: لم governs a مضارع مجزوم, so أَقُل behind it is أقول
  // contracted rather than the noun أقل, and the stemmer orders its candidates
  // accordingly. Empty when the word did not come off a page.
  DictionaryLookupResult lookup(const std::string& rawWord, const std::string& previousWord = std::string(),
                                bool includeSuggestions = true);
  // The article behind a reading the caller already holds, read without searching
  // the index again. Returns false when the bytes could not be read back.
  bool readReading(const DictionaryReading& reading, std::string& definition, bool& truncated);
  std::vector<std::string> getHistory();
  void addHistory(const std::string& word);
  void clearHistory();

  static std::string cleanWord(const std::string& word);

 private:
  DictionaryStore() = default;

  struct IndexHit {
    std::string headword;
    uint32_t dictOffset = 0;
    uint32_t dictSize = 0;
  };

  static constexpr const char* CONFIG_PATH = "/.crosspoint/dictionary_config.json";
  static constexpr const char* HISTORY_PATH = "/.crosspoint/dictionary_history.txt";
  static constexpr size_t MAX_SCAN_ENTRIES = 64;
  static constexpr size_t MAX_DEFINITION_BYTES = 16384;
  static constexpr size_t MIN_DEFINITION_BYTES = 1024;
  static constexpr size_t MAX_HISTORY_ITEMS = 15;
  // What the "See also" line can hold and how far down the candidate list it is
  // worth looking for one. Every probe past the answer is an index search the
  // reader is not waiting for a result from, so the walk stops early rather than
  // running the whole chain on the card.
  static constexpr size_t MAX_READINGS = 6;
  static constexpr size_t MAX_READING_PROBES = 14;

  std::vector<DictionaryEntry> entries;
  DictionaryEntry activeOnlyEntry;
  std::string activeIfoPath;
  int activeIndex = -1;
  bool configLoaded = false;
  bool scanned = false;
  bool activeOnlyLoaded = false;
  uint8_t definitionTextSize = DEF_TEXT_SMALL;

  bool loadEntryFromIfoPath(const std::string& ifoPath, DictionaryEntry& entry) const;
  bool ensureActiveEntryLoaded();
  void clearActiveOnlyEntry();
  DictionaryEntry* activeEntry();
  const DictionaryEntry* activeEntry() const;
  bool ensurePrepared(DictionaryEntry& entry, const std::function<void(int percent)>& onProgress = nullptr);
  bool loadCheckpointCache(DictionaryEntry& entry);
  bool saveCheckpointCache(const DictionaryEntry& entry) const;
  bool findIndexHit(const DictionaryEntry& entry, const std::string& word, IndexHit& hit) const;
  bool lookupSynonym(const DictionaryEntry& entry, const std::string& word, std::string& canonical) const;
  std::string headwordAtOrdinal(const DictionaryEntry& entry, uint32_t ordinal) const;
  std::string readDefinition(const DictionaryEntry& entry, const IndexHit& hit, bool& truncated,
                             DictionaryLookupResult::Status* outFailureStatus = nullptr) const;
  std::vector<std::string> findSuggestions(const DictionaryEntry& entry, const std::string& word, int maxResults) const;
};

#define DICTIONARIES DictionaryStore::getInstance()
