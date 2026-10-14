#include "StarDictIndex.h"

#include <algorithm>
#include <cctype>

namespace StarDictIndex {
namespace {

// Buffered scanning: one file read per buffer rather than one per byte. On the
// device every Reader::read() takes storageMutex, so a per-byte scan of a 2.7MB
// index cost millions of mutex round-trips before any I/O.
constexpr size_t SCAN_BUFFER_BYTES = 4096;
constexpr uint32_t YIELD_INTERVAL_BYTES = 64 * 1024;

}  // namespace

uint32_t readBE32(const uint8_t* buf) {
  return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
         (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
}

std::string readKey(Reader& file) {
  // No reserve(): reserving past the small-string buffer always allocates, and
  // most keys fit in it. The fuzzy scan calls this thousands of times per
  // lookup, so a reserve here was thousands of malloc/free pairs for nothing.
  std::string key;
  while (true) {
    const int c = file.read();
    if (c <= 0) break;  // EOF/error, or the NUL terminator
    if (key.size() < MAX_KEY_BYTES) key.push_back(static_cast<char>(c));
    // else: keep consuming so the record after the key stays aligned.
  }
  return key;
}

int compareKeys(const std::string& lhs, const std::string& rhs) {
  const unsigned char* a = reinterpret_cast<const unsigned char*>(lhs.c_str());
  const unsigned char* b = reinterpret_cast<const unsigned char*>(rhs.c_str());
  while (*a && *b) {
    const int ca = (*a < 0x80) ? std::tolower(*a) : *a;
    const int cb = (*b < 0x80) ? std::tolower(*b) : *b;
    if (ca != cb) return ca - cb;
    ++a;
    ++b;
  }
  const int ca = (*a < 0x80) ? std::tolower(*a) : *a;
  const int cb = (*b < 0x80) ? std::tolower(*b) : *b;
  return ca - cb;
}

bool buildCheckpoints(Reader& file, const size_t recordBytes, CheckpointTable& out, const ScanHooks& hooks) {
  out.clear();
  out.fileSize = static_cast<uint32_t>(file.fileSize());
  if (out.fileSize == 0 || !file.seekSet(0)) return false;

  bool limitReached = false;
  auto addCheckpoint = [&](const uint32_t pos, const uint32_t ordinal) {
    if (limitReached) return true;
    if (out.offsets.size() >= MAX_CHECKPOINT_COUNT) {
      limitReached = true;
      return true;
    }
    if (out.offsets.size() == out.offsets.capacity()) {
      const size_t nextCapacity =
          std::min<size_t>(MAX_CHECKPOINT_COUNT, out.offsets.empty() ? 128 : out.offsets.capacity() * 2);
      if (hooks.hasHeadroom && !hooks.hasHeadroom(nextCapacity * sizeof(uint32_t) * 2)) {
        // A partial table still answers every lookup; it just walks further from
        // the last checkpoint. Only an empty one is a failure.
        if (out.offsets.empty()) return false;
        limitReached = true;
        return true;
      }
      out.offsets.reserve(nextCapacity);
      out.ordinals.reserve(nextCapacity);
    }
    out.offsets.push_back(pos);
    out.ordinals.push_back(ordinal);
    return true;
  };

  uint8_t buffer[SCAN_BUFFER_BYTES];
  size_t bufLen = 0;
  size_t bufPos = 0;
  const auto ensureBuffered = [&]() -> bool {
    if (bufPos < bufLen) return true;
    const int got = file.read(buffer, SCAN_BUFFER_BYTES);
    if (got <= 0) return false;
    bufLen = static_cast<size_t>(got);
    bufPos = 0;
    return true;
  };

  uint32_t pos = 0;
  uint32_t nextYieldPos = YIELD_INTERVAL_BYTES;
  int lastProgress = -1;
  while (pos < out.fileSize) {
    if (out.totalEntries % CHECKPOINT_INTERVAL == 0) {
      if (!addCheckpoint(pos, out.totalEntries)) {
        out.clear();
        return false;
      }
    }

    bool sawTerminator = false;
    while (ensureBuffered()) {
      const uint8_t c = buffer[bufPos++];
      ++pos;
      if (c == 0) {
        sawTerminator = true;
        break;
      }
    }
    // No terminator, or nothing left for the record: truncated tail.
    if (!sawTerminator || pos >= out.fileSize) break;

    size_t remaining = recordBytes;
    while (remaining > 0 && ensureBuffered()) {
      const size_t take = std::min(remaining, bufLen - bufPos);
      bufPos += take;
      pos += static_cast<uint32_t>(take);
      remaining -= take;
    }
    if (remaining != 0) break;
    ++out.totalEntries;

    // Feeds the watchdog and lets the render/input task run: without it the scan
    // holds the CPU for the whole file.
    if (hooks.yield && pos >= nextYieldPos) {
      nextYieldPos = pos + YIELD_INTERVAL_BYTES;
      hooks.yield();
    }
    if (hooks.progress) {
      const int progress = static_cast<int>((static_cast<uint64_t>(pos) * 100ULL) / out.fileSize);
      if (progress >= lastProgress + 5) {
        lastProgress = progress;
        hooks.progress(hooks.progressCtx, progress);
      }
    }
  }

  return !out.offsets.empty();
}

namespace {

// The last checkpoint whose first key sorts at or before `word`.
int segmentFor(Reader& file, const CheckpointTable& table, const std::string& word) {
  int lo = 0;
  int hi = static_cast<int>(table.offsets.size()) - 1;
  while (lo < hi) {
    const int mid = lo + (hi - lo + 1) / 2;
    if (!file.seekSet(table.offsets[mid])) break;
    if (compareKeys(readKey(file), word) <= 0) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

// Entries between checkpoint `segment` and the next one, or the end of file.
uint32_t entriesInSegment(const CheckpointTable& table, const int segment) {
  if (segment + 1 < static_cast<int>(table.ordinals.size())) {
    return table.ordinals[segment + 1] - table.ordinals[segment];
  }
  if (table.totalEntries > table.ordinals[segment]) return table.totalEntries - table.ordinals[segment];
  return CHECKPOINT_INTERVAL;
}

}  // namespace

bool findKey(Reader& file, const CheckpointTable& table, const size_t recordBytes, const std::string& word, Hit& hit) {
  if (table.empty() || recordBytes > IDX_RECORD_BYTES) return false;

  const int segment = segmentFor(file, table, word);
  if (!file.seekSet(table.offsets[segment])) return false;
  const uint32_t maxEntries = entriesInSegment(table, segment);

  for (uint32_t i = 0; i < maxEntries; ++i) {
    const std::string key = readKey(file);
    if (key.empty()) break;
    uint8_t record[IDX_RECORD_BYTES] = {};
    if (file.read(record, recordBytes) != static_cast<int>(recordBytes)) break;
    const int cmp = compareKeys(key, word);
    if (cmp == 0) {
      hit.key = key;
      std::copy(record, record + recordBytes, hit.record);
      return true;
    }
    // Keys are sorted, so the first one past `word` settles it.
    if (cmp > 0) break;
  }
  return false;
}

std::string keyAtOrdinal(Reader& file, const CheckpointTable& table, const size_t recordBytes, const uint32_t ordinal) {
  if (table.empty()) return "";

  size_t lo = 0;
  size_t hi = table.ordinals.size();
  while (lo + 1 < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (table.ordinals[mid] <= ordinal) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  if (table.ordinals[lo] > ordinal) return "";
  if (!file.seekSet(table.offsets[lo])) return "";

  uint8_t skip[IDX_RECORD_BYTES];
  for (uint32_t i = ordinal - table.ordinals[lo]; i > 0; --i) {
    readKey(file);
    if (file.read(skip, recordBytes) != static_cast<int>(recordBytes)) return "";
  }
  return readKey(file);
}

bool findSynonym(Reader& syn, const CheckpointTable& synTable, const std::string& word, uint32_t& ordinal) {
  // Checkpoints, not a seek-and-resync scan. Resyncing meant seeking to an
  // arbitrary offset and taking the next NUL for a key terminator -- but the
  // ordinal following each key is four raw bytes, and any ordinal below
  // 0x01000000 carries NULs of its own. Landing in one made the bytes after it
  // read as a key, which mis-narrowed the search and lost the entry: on a .syn
  // whose ordinals all fit in 16 bits, most keys were simply unreachable.
  Hit hit;
  if (!findKey(syn, synTable, SYN_RECORD_BYTES, word, hit)) return false;
  ordinal = readBE32(hit.record);
  return true;
}

uint32_t seekToNeighbourhood(Reader& file, const CheckpointTable& table, const std::string& anchor,
                             const int segmentsEitherSide) {
  if (table.empty()) return 0;

  const int segment = segmentFor(file, table, anchor);
  const int startSegment = std::max(0, segment - segmentsEitherSide);
  const int endSegment = std::min(static_cast<int>(table.offsets.size()) - 1, segment + segmentsEitherSide);
  if (!file.seekSet(table.offsets[startSegment])) return 0;

  uint32_t total = static_cast<uint32_t>(endSegment - startSegment + 1) * CHECKPOINT_INTERVAL;
  if (table.totalEntries > table.ordinals[startSegment]) {
    total = std::min<uint32_t>(total, table.totalEntries - table.ordinals[startSegment]);
  }
  return total;
}

}  // namespace StarDictIndex
