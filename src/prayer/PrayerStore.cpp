#include "prayer/PrayerStore.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "MidadAppSettings.h"
#include "prayer/PrayerMethods.h"
#include "util/TimeUtils.h"

namespace PrayerStore {

namespace {
constexpr int SCHEMA_VERSION = 1;
constexpr unsigned NEXT_MONTH_THRESHOLD_DAYS = 7;

// ArduinoJson's generic reader calls read() per byte, and every HalFile::read()
// takes the storage mutex -- 45k round-trips for one Aladhan response. Buffer
// it. ArduinoJson picks this class up through its readBytes() overload.
class BufferedHalFileReader {
 public:
  explicit BufferedHalFileReader(HalFile& file) : file_(file) {}

  int read() {
    if (pos_ >= len_ && !fill()) return -1;
    return static_cast<unsigned char>(buf_[pos_++]);
  }

  size_t readBytes(char* out, const size_t count) {
    size_t copied = 0;
    while (copied < count) {
      if (pos_ >= len_ && !fill()) break;
      const size_t chunk = (len_ - pos_ < count - copied) ? len_ - pos_ : count - copied;
      memcpy(out + copied, buf_ + pos_, chunk);
      pos_ += chunk;
      copied += chunk;
    }
    return copied;
  }

 private:
  bool fill() {
    const int got = file_.read(buf_, sizeof(buf_));
    if (got <= 0) return false;
    len_ = static_cast<size_t>(got);
    pos_ = 0;
    return true;
  }

  HalFile& file_;
  char buf_[256];
  size_t pos_ = 0;
  size_t len_ = 0;
};

bool fingerprintMatches(JsonVariantConst doc) {
  const int32_t lat = doc["lat"] | INT32_MIN;
  const int32_t lng = doc["lng"] | INT32_MIN;
  const int method = doc["method"] | -1;
  return lat == MIDAD_APP_SETTINGS.prayerLatE2 && lng == MIDAD_APP_SETTINGS.prayerLngE2 &&
         method == PrayerMethods::effectiveAladhanId();
}

// Removes month files for which keep(y, m) is false.
template <typename Keep>
void removeMonths(Keep keep) {
  // Deleting while iterating a FAT directory is undefined, so each pass
  // collects a batch of names, closes the handle, then deletes. A full batch
  // means there may be more, so the loop runs again -- clearMonths() must
  // leave nothing behind even if a directory somehow accumulated files.
  constexpr int BATCH = 8;
  constexpr size_t NAME_LEN = 16;  // "YYYY-MM.json" + terminator
  bool batchWasFull = true;
  while (batchWasFull) {
    HalFile dir = Storage.open(DIR);
    if (!dir || !dir.isDirectory()) return;
    char name[24];
    char doomed[BATCH][NAME_LEN];
    int doomedCount = 0;
    for (auto entry = dir.openNextFile(); entry && doomedCount < BATCH; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      int y;
      unsigned m;
      if (!PrayerPure::parseMonthFileName(name, y, m) || keep(y, m)) continue;
      strlcpy(doomed[doomedCount], name, NAME_LEN);
      ++doomedCount;
    }
    dir.close();
    if (doomedCount == 0) return;
    char path[48];
    for (int i = 0; i < doomedCount; ++i) {
      snprintf(path, sizeof(path), "%s/%s", DIR, doomed[i]);
      Storage.remove(path);
      LOG_DBG("PRAY", "Removed %s", path);
    }
    batchWasFull = doomedCount == BATCH;
  }
}
}  // namespace

void monthPath(char* buf, const size_t len, const int year, const unsigned month) {
  snprintf(buf, len, "%s/%04d-%02u.json", DIR, year, month);
}

bool hasMonth(const int year, const unsigned month) {
  char path[48];
  monthPath(path, sizeof(path), year, month);
  return Storage.exists(path);
}

bool loadDay(const uint32_t dayOrdinal, PrayerDay& out) {
  memset(&out, 0, sizeof(out));
  int year;
  unsigned month, day;
  if (dayOrdinal == 0 || !TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) return false;

  char path[48];
  monthPath(path, sizeof(path), year, month);
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path, doc)) return false;
  if ((doc["v"] | 0) != SCHEMA_VERSION || !fingerprintMatches(doc.as<JsonVariantConst>())) {
    LOG_DBG("PRAY", "%s: stale fingerprint", path);
    return false;
  }
  JsonArrayConst days = doc["d"].as<JsonArrayConst>();
  if (day < 1 || day > days.size()) return false;
  JsonArrayConst row = days[day - 1].as<JsonArrayConst>();
  if (row.size() < static_cast<size_t>(PrayerPure::TIMING_COUNT) + 3) return false;
  for (int i = 0; i < PrayerPure::TIMING_COUNT; ++i) {
    if (!PrayerPure::copyTime(row[i] | "", out.times[i], sizeof(out.times[i]))) return false;
  }
  out.hijriDay = row[PrayerPure::TIMING_COUNT] | 0;
  const int hijriMonth = row[PrayerPure::TIMING_COUNT + 1] | 0;
  out.hijriMonth = (hijriMonth >= 1 && hijriMonth <= 12) ? static_cast<uint8_t>(hijriMonth) : 0;
  out.hijriYear = row[PrayerPure::TIMING_COUNT + 2] | 0;
  return true;
}

bool needsNextMonth(const uint32_t dayOrdinal) {
  int year;
  unsigned month, day;
  if (!TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) return false;
  return TimeUtils::daysInMonth(year, month) - day < NEXT_MONTH_THRESHOLD_DAYS;
}

bool importAladhan(const char* tmpPath, const int year, const unsigned month, const int32_t latE2, const int32_t lngE2,
                   const uint8_t method) {
  HalFile file;
  if (!Storage.openFileForRead("PRAY", tmpPath, file)) return false;

  // Keep only what the month file needs: 31 x (6 short strings + hijri parts)
  // instead of the ~45KB the full response would cost to DOM while TLS is
  // still holding its buffers.
  JsonDocument filter;
  JsonObject day = filter["data"][0].to<JsonObject>();
  for (const char* key : PrayerPure::TIMING_KEYS) day["timings"][key] = true;
  day["date"]["hijri"]["day"] = true;
  day["date"]["hijri"]["month"]["number"] = true;
  day["date"]["hijri"]["year"] = true;

  LOG_DBG("PRAY", "import %04d-%02u: free heap before parse %u", year, month, ESP.getFreeHeap());
  JsonDocument doc;
  BufferedHalFileReader reader(file);
  const DeserializationError err = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  file.close();
  if (err) {
    LOG_ERR("PRAY", "import %04d-%02u: bad JSON (%s)", year, month, err.c_str());
    return false;
  }
  LOG_DBG("PRAY", "import %04d-%02u: free heap after parse %u", year, month, ESP.getFreeHeap());

  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  const unsigned expectedDays = TimeUtils::daysInMonth(year, month);
  if (data.size() != expectedDays) {
    LOG_ERR("PRAY", "import %04d-%02u: %u days, expected %u", year, month, static_cast<unsigned>(data.size()),
            expectedDays);
    return false;
  }

  JsonDocument out;
  out["v"] = SCHEMA_VERSION;
  out["y"] = year;
  out["m"] = month;
  out["lat"] = latE2;
  out["lng"] = lngE2;
  out["method"] = method;
  JsonArray days = out["d"].to<JsonArray>();
  char hhmm[6];
  for (JsonObjectConst d : data) {
    JsonArray row = days.add<JsonArray>();
    JsonObjectConst timings = d["timings"];
    for (const char* key : PrayerPure::TIMING_KEYS) {
      if (!PrayerPure::copyTime(timings[key] | "", hhmm, sizeof(hhmm))) {
        LOG_ERR("PRAY", "import %04d-%02u: bad %s", year, month, key);
        return false;
      }
      row.add(hhmm);  // copied: ArduinoJson duplicates char* values
    }
    // day/year arrive as strings ("18", "1448"), the month number as an int.
    JsonObjectConst h = d["date"]["hijri"];
    row.add(atoi(h["day"] | "0"));
    row.add(h["month"]["number"] | 0);
    row.add(atoi(h["year"] | "0"));
  }
  doc.clear();

  Storage.mkdir(DIR);
  char path[48];
  monthPath(path, sizeof(path), year, month);
  return PersistableStoreBase::writeDocToFile(path, out);
}

void pruneBefore(const int year, const unsigned month) {
  removeMonths([year, month](const int y, const unsigned m) { return y > year || (y == year && m >= month); });
}

void clearMonths() {
  removeMonths([](int, unsigned) { return false; });
}

}  // namespace PrayerStore
