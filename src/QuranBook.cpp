#include "QuranBook.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MidadAppSettings.h"
#include "util/BookReaderSettings.h"

namespace {

// Same cache-dir formula as the Epub constructor (path-hash keyed).
std::string quranCachePath() {
  return "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(std::string(QuranBook::PATH)));
}

// Give the Quran its reading face: a per-book sidecar selecting the built-in
// KFGQPC Uthmanic Hafs family (the Madinah Mushaf's own typeface -- renders
// full harakat with our static pipeline; Noto Naskh Arabic is the other
// option in the drawer's Font Name picker). Written only when no sidecar
// exists yet, so any choice the user later makes in the reader drawer sticks
// across re-extractions and firmware updates.
void writeDefaultSidecarIfMissing() {
  const std::string cacheDir = quranCachePath();
  const std::string sidecar = cacheDir + BookReaderSettings::FILE_NAME;
  if (Storage.exists(sidecar.c_str())) return;
  Storage.mkdir(cacheDir.c_str());

  uint8_t buf[BookReaderSettings::PAYLOAD_SIZE];
  memset(buf, 0, sizeof(buf));
  buf[0] = 'B';
  buf[1] = 'K';
  buf[2] = BookReaderSettings::FORMAT_VERSION;
  buf[3] = CrossPointSettings::BOOK_NO_OVERRIDE;  // fontSize: inherit
  buf[4] = CrossPointSettings::BOOK_NO_OVERRIDE;  // arabicFontSize: inherit
  buf[5] = CrossPointSettings::BOOK_NO_OVERRIDE;  // lineSpacing: inherit
  buf[6] = CrossPointSettings::JUSTIFIED;         // mushaf convention: justified lines
  buf[7] = CrossPointSettings::UTHMANICHAFS;      // built-in Arabic family: KFGQPC Uthmanic Hafs
  // Arabic family-source: force the built-in path (buf[8..39] Latin stays "").
  buf[8 + 32] = CrossPointSettings::BOOK_FORCE_BUILTIN_FAMILY[0];

  HalFile f;
  if (Storage.openFileForWrite("QURAN", sidecar, f)) {
    f.write(buf, sizeof(buf));
    LOG_INF("QURAN", "Default Quran font sidecar written (Uthmanic Hafs)");
  }
}

}  // namespace

#ifndef SIMULATOR
// Embedded by board_build.embed_files = data/quran.epub (see platformio.ini).
extern const uint8_t _binary_data_quran_epub_start[] asm("_binary_data_quran_epub_start");
extern const uint8_t _binary_data_quran_epub_end[] asm("_binary_data_quran_epub_end");
#endif

namespace QuranBook {

const uint8_t* data(size_t& size) {
#ifdef SIMULATOR
  // The native simulator build has no flash embedding; read the repo asset
  // from the host filesystem (the simulator runs from the repo root).
  static std::vector<uint8_t> hostData;
  if (hostData.empty()) {
    FILE* f = fopen("data/quran.epub", "rb");
    if (!f) {
      size = 0;
      return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    hostData.resize(len);
    if (fread(hostData.data(), 1, len, f) != static_cast<size_t>(len)) {
      hostData.clear();
    }
    fclose(f);
  }
  size = hostData.size();
  return hostData.empty() ? nullptr : hostData.data();
#else
  // cppcheck-suppress comparePointers // linker-provided bounds of one blob, see below
  // False positive: cppcheck sees two unrelated extern symbols. They are the
  // start/end markers the linker emits around a single embedded blob
  // (board_build.embed_files = data/quran.epub in platformio.ini), so they do
  // point into the same object and this subtraction is the standard ESP-IDF
  // idiom for its length.
  size = static_cast<size_t>(_binary_data_quran_epub_end - _binary_data_quran_epub_start);
  return _binary_data_quran_epub_start;
#endif
}

bool isPinned() { return MIDAD_APP_SETTINGS.quranEnabled != 0 && Storage.exists(PATH); }

bool ensureExtracted() {
  size_t embeddedSize = 0;
  const uint8_t* embedded = data(embeddedSize);
  if (!embedded || embeddedSize == 0) {
    LOG_ERR("QURAN", "Embedded Quran data unavailable");
    return false;
  }

  // Already extracted and whole (a size mismatch means a torn previous write
  // or an older firmware's copy -- re-extract to match this build).
  if (Storage.exists(PATH)) {
    HalFile existing;
    if (Storage.openFileForRead("QURAN", PATH, existing) && existing.size() == embeddedSize) {
      writeDefaultSidecarIfMissing();
      return true;
    }
  }

  // The embedded text changed (firmware update): the parsed cache under the
  // Quran's cache dir describes the OLD content -- clear it or the reader
  // keeps showing stale pages. The user's font/size sidecar survives.
  {
    const std::string cacheDir = quranCachePath();
    const std::string sidecar = cacheDir + BookReaderSettings::FILE_NAME;
    std::vector<uint8_t> savedSidecar;
    HalFile sf;
    if (Storage.openFileForRead("QURAN", sidecar, sf)) {
      savedSidecar.resize(sf.size());
      if (sf.read(savedSidecar.data(), savedSidecar.size()) != static_cast<int>(savedSidecar.size())) {
        savedSidecar.clear();
      }
    }
    Storage.removeDir(cacheDir.c_str());
    if (!savedSidecar.empty()) {
      Storage.mkdir(cacheDir.c_str());
      HalFile rf;
      if (Storage.openFileForWrite("QURAN", sidecar, rf)) {
        rf.write(savedSidecar.data(), savedSidecar.size());
      }
    }
  }

  Storage.mkdir(DIR);
  HalFile out;
  if (!Storage.openFileForWrite("QURAN", PATH, out)) {
    LOG_ERR("QURAN", "Cannot open %s for write", PATH);
    return false;
  }
  // Chunked copy: flash-resident source, bounded RAM, SD-friendly block size.
  constexpr size_t CHUNK = 4096;
  size_t written = 0;
  while (written < embeddedSize) {
    const size_t n = std::min(CHUNK, embeddedSize - written);
    if (out.write(embedded + written, n) != n) {
      LOG_ERR("QURAN", "Short write extracting Quran at %u/%u", (unsigned)written, (unsigned)embeddedSize);
      return false;
    }
    written += n;
  }
  out.flush();
  LOG_INF("QURAN", "Extracted %s (%u bytes)", PATH, (unsigned)embeddedSize);
  writeDefaultSidecarIfMissing();
  return true;
}

const char* surahName(const int surah) {
  static constexpr const char* kNames[] = {
      "Al-Fatihah",   "Al-Baqarah",  "Ali 'Imran",    "An-Nisa",        "Al-Ma'idah",
      "Al-An'am",     "Al-A'raf",    "Al-Anfal",      "At-Tawbah",      "Yunus",
      "Hud",          "Yusuf",       "Ar-Ra'd",       "Ibrahim",        "Al-Hijr",
      "An-Nahl",      "Al-Isra",     "Al-Kahf",       "Maryam",         "Taha",
      "Al-Anbya",     "Al-Hajj",     "Al-Mu'minun",   "An-Nur",         "Al-Furqan",
      "Ash-Shu'ara",  "An-Naml",     "Al-Qasas",      "Al-'Ankabut",    "Ar-Rum",
      "Luqman",       "As-Sajdah",   "Al-Ahzab",      "Saba",           "Fatir",
      "Ya-Sin",       "As-Saffat",   "Sad",           "Az-Zumar",       "Ghafir",
      "Fussilat",     "Ash-Shuraa",  "Az-Zukhruf",    "Ad-Dukhan",      "Al-Jathiyah",
      "Al-Ahqaf",     "Muhammad",    "Al-Fath",       "Al-Hujurat",     "Qaf",
      "Adh-Dhariyat", "At-Tur",      "An-Najm",       "Al-Qamar",       "Ar-Rahman",
      "Al-Waqi'ah",   "Al-Hadid",    "Al-Mujadila",   "Al-Hashr",       "Al-Mumtahanah",
      "As-Saf",       "Al-Jumu'ah",  "Al-Munafiqun",  "At-Taghabun",    "At-Talaq",
      "At-Tahrim",    "Al-Mulk",     "Al-Qalam",      "Al-Haqqah",      "Al-Ma'arij",
      "Nuh",          "Al-Jinn",     "Al-Muzzammil",  "Al-Muddaththir", "Al-Qiyamah",
      "Al-Insan",     "Al-Mursalat", "An-Naba",       "An-Nazi'at",     "'Abasa",
      "At-Takwir",    "Al-Infitar",  "Al-Mutaffifin", "Al-Inshiqaq",    "Al-Buruj",
      "At-Tariq",     "Al-A'la",     "Al-Ghashiyah",  "Al-Fajr",        "Al-Balad",
      "Ash-Shams",    "Al-Layl",     "Ad-Duhaa",      "Ash-Sharh",      "At-Tin",
      "Al-'Alaq",     "Al-Qadr",     "Al-Bayyinah",   "Az-Zalzalah",    "Al-'Adiyat",
      "Al-Qari'ah",   "At-Takathur", "Al-'Asr",       "Al-Humazah",     "Al-Fil",
      "Quraysh",      "Al-Ma'un",    "Al-Kawthar",    "Al-Kafirun",     "An-Nasr",
      "Al-Masad",     "Al-Ikhlas",   "Al-Falaq",      "An-Nas",
  };
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == 114, "one name per surah");
  return surah >= 1 && surah <= 114 ? kNames[surah - 1] : "";
}

const JuzRange* juzRange(const int juz) {
  // Same starts as JUZ_STARTS in tools/quran/build_quran_epub_kfgqpc.py.
  static constexpr JuzRange kJuz[] = {
      {1, 1, 2, 141},   {2, 142, 2, 252}, {2, 253, 3, 92},  {3, 93, 4, 23},    {4, 24, 4, 147},  {4, 148, 5, 81},
      {5, 82, 6, 110},  {6, 111, 7, 87},  {7, 88, 8, 40},   {8, 41, 9, 92},    {9, 93, 11, 5},   {11, 6, 12, 52},
      {12, 53, 14, 52}, {15, 1, 16, 128}, {17, 1, 18, 74},  {18, 75, 20, 135}, {21, 1, 22, 78},  {23, 1, 25, 20},
      {25, 21, 27, 55}, {27, 56, 29, 45}, {29, 46, 33, 30}, {33, 31, 36, 27},  {36, 28, 39, 31}, {39, 32, 41, 46},
      {41, 47, 45, 37}, {46, 1, 51, 30},  {51, 31, 57, 29}, {58, 1, 66, 12},   {67, 1, 77, 50},  {78, 1, 114, 6},
  };
  static_assert(sizeof(kJuz) / sizeof(kJuz[0]) == 30, "one range per juz");
  return juz >= 1 && juz <= 30 ? &kJuz[juz - 1] : nullptr;
}

}  // namespace QuranBook
