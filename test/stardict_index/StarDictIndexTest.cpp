// Index and synonym probing against generated StarDict files.
//
// The fixtures are built byte by byte here rather than shipped as binaries, so
// the layout a test depends on (record size, ordinal bytes, a truncated tail) is
// visible in the test that depends on it.

#include <StarDictIndex.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using StarDictIndex::CheckpointTable;
using StarDictIndex::Hit;

class MemoryReader : public StarDictIndex::Reader {
 public:
  explicit MemoryReader(std::vector<uint8_t> bytes) : data(std::move(bytes)) {}

  bool seekSet(size_t pos) override {
    if (pos > data.size()) return false;
    cursor = pos;
    return true;
  }
  int read(void* buf, size_t bytes) override {
    const size_t available = std::min(bytes, data.size() - cursor);
    if (available == 0) return 0;
    std::copy(data.begin() + static_cast<long>(cursor), data.begin() + static_cast<long>(cursor + available),
              static_cast<uint8_t*>(buf));
    cursor += available;
    return static_cast<int>(available);
  }
  int read() override {
    if (cursor >= data.size()) return -1;
    return data[cursor++];
  }
  size_t position() const override { return cursor; }
  size_t fileSize() override { return data.size(); }

 private:
  std::vector<uint8_t> data;
  size_t cursor = 0;
};

// Same interface over a real file, so at least one test exercises seek/read
// against the filesystem rather than a vector.
class TempFileReader : public StarDictIndex::Reader {
 public:
  explicit TempFileReader(const std::vector<uint8_t>& bytes) {
    path = (std::filesystem::temp_directory_path() / "crosspoint_stardict_test.idx").string();
    FILE* out = std::fopen(path.c_str(), "wb");
    if (out) {
      std::fwrite(bytes.data(), 1, bytes.size(), out);
      std::fclose(out);
    }
    file = std::fopen(path.c_str(), "rb");
    bytesTotal = bytes.size();
  }
  ~TempFileReader() override {
    if (file) std::fclose(file);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }

  bool seekSet(size_t pos) override { return file && std::fseek(file, static_cast<long>(pos), SEEK_SET) == 0; }
  int read(void* buf, size_t bytes) override {
    if (!file) return -1;
    return static_cast<int>(std::fread(buf, 1, bytes, file));
  }
  int read() override {
    if (!file) return -1;
    const int c = std::fgetc(file);
    return c == EOF ? -1 : c;
  }
  size_t position() const override { return file ? static_cast<size_t>(std::ftell(file)) : 0; }
  size_t fileSize() override { return bytesTotal; }

 private:
  std::string path;
  FILE* file = nullptr;
  size_t bytesTotal = 0;
};

void appendKey(std::vector<uint8_t>& out, const std::string& key) {
  out.insert(out.end(), key.begin(), key.end());
  out.push_back(0);
}

void appendBE32(std::vector<uint8_t>& out, const uint32_t value) {
  out.push_back(static_cast<uint8_t>(value >> 24));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

// key\0 + BE32 offset + BE32 size, with the offset derived from the ordinal so a
// hit can be checked against the key it came from.
std::vector<uint8_t> makeIdx(const std::vector<std::string>& sortedKeys) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < sortedKeys.size(); ++i) {
    appendKey(out, sortedKeys[i]);
    appendBE32(out, static_cast<uint32_t>(i) * 100);
    appendBE32(out, static_cast<uint32_t>(sortedKeys[i].size()));
  }
  return out;
}

// key\0 + BE32 ordinal.
std::vector<uint8_t> makeSyn(const std::vector<std::pair<std::string, uint32_t>>& sortedEntries) {
  std::vector<uint8_t> out;
  for (const auto& entry : sortedEntries) {
    appendKey(out, entry.first);
    appendBE32(out, entry.second);
  }
  return out;
}

std::vector<std::string> numberedKeys(const size_t count) {
  std::vector<std::string> keys;
  keys.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "k%05zu", i);
    keys.emplace_back(buf);
  }
  return keys;
}

// 2600 keys puts checkpoints at ordinals 0, 1024 and 2048, so the boundary cases
// are reachable.
constexpr size_t MANY = 2600;

CheckpointTable buildIdxTable(StarDictIndex::Reader& reader) {
  CheckpointTable table;
  EXPECT_TRUE(StarDictIndex::buildCheckpoints(reader, StarDictIndex::IDX_RECORD_BYTES, table));
  return table;
}

TEST(StarDictIndexCheckpoints, CountsEntriesAndSpacesCheckpoints) {
  MemoryReader reader(makeIdx(numberedKeys(MANY)));
  const CheckpointTable table = buildIdxTable(reader);

  EXPECT_EQ(table.totalEntries, MANY);
  EXPECT_EQ(table.offsets.size(), 3u);
  EXPECT_EQ(table.ordinals, (std::vector<uint32_t>{0, 1024, 2048}));
  EXPECT_EQ(table.offsets[0], 0u);
  // Every entry is "kNNNNN" + NUL + 8 record bytes = 15 bytes.
  EXPECT_EQ(table.offsets[1], 1024u * 15u);
}

TEST(StarDictIndexCheckpoints, EmptyFileHasNoTable) {
  MemoryReader reader({});
  CheckpointTable table;
  EXPECT_FALSE(StarDictIndex::buildCheckpoints(reader, StarDictIndex::IDX_RECORD_BYTES, table));
  EXPECT_TRUE(table.empty());
}

TEST(StarDictIndexCheckpoints, TruncatedTailKeepsTheEntriesBeforeIt) {
  std::vector<uint8_t> bytes = makeIdx(numberedKeys(MANY));
  bytes.resize(bytes.size() - 5);  // half of the last record
  MemoryReader reader(std::move(bytes));
  CheckpointTable table;
  ASSERT_TRUE(StarDictIndex::buildCheckpoints(reader, StarDictIndex::IDX_RECORD_BYTES, table));

  EXPECT_EQ(table.totalEntries, MANY - 1);
  Hit hit;
  EXPECT_TRUE(StarDictIndex::findKey(reader, table, StarDictIndex::IDX_RECORD_BYTES, "k02598", hit));
  EXPECT_FALSE(StarDictIndex::findKey(reader, table, StarDictIndex::IDX_RECORD_BYTES, "k02599", hit));
}

TEST(StarDictIndexCheckpoints, NoHeadroomAbortsWithoutAPartialTable) {
  MemoryReader reader(makeIdx(numberedKeys(MANY)));
  StarDictIndex::ScanHooks hooks;
  hooks.hasHeadroom = [](size_t) { return false; };
  CheckpointTable table;

  EXPECT_FALSE(StarDictIndex::buildCheckpoints(reader, StarDictIndex::IDX_RECORD_BYTES, table, hooks));
  EXPECT_TRUE(table.empty());
}

TEST(StarDictIndexCheckpoints, ProgressIsMonotonicAndYieldsFire) {
  static std::vector<int> reported;
  static int yields;
  reported.clear();
  yields = 0;

  MemoryReader reader(makeIdx(numberedKeys(MANY * 4)));
  StarDictIndex::ScanHooks hooks;
  hooks.yield = [] { ++yields; };
  hooks.progress = [](void*, int percent) { reported.push_back(percent); };
  CheckpointTable table;
  ASSERT_TRUE(StarDictIndex::buildCheckpoints(reader, StarDictIndex::IDX_RECORD_BYTES, table, hooks));

  ASSERT_FALSE(reported.empty());
  for (size_t i = 1; i < reported.size(); ++i) EXPECT_GT(reported[i], reported[i - 1]);
  EXPECT_LE(reported.back(), 100);
  EXPECT_GT(yields, 0);
}

TEST(StarDictIndexFindKey, HitsEveryCheckpointBoundary) {
  MemoryReader reader(makeIdx(numberedKeys(MANY)));
  const CheckpointTable table = buildIdxTable(reader);

  for (const size_t ordinal :
       {size_t{0}, size_t{1}, size_t{1023}, size_t{1024}, size_t{1025}, size_t{2047}, size_t{2048}, size_t{2599}}) {
    char key[16];
    std::snprintf(key, sizeof(key), "k%05zu", ordinal);
    Hit hit;
    ASSERT_TRUE(StarDictIndex::findKey(reader, table, StarDictIndex::IDX_RECORD_BYTES, key, hit)) << key;
    EXPECT_EQ(hit.key, key);
    EXPECT_EQ(StarDictIndex::readBE32(hit.record), ordinal * 100);
  }
}

TEST(StarDictIndexFindKey, MissesBetweenAndOutsideKeys) {
  MemoryReader reader(makeIdx({"apple", "banana", "cherry"}));
  const CheckpointTable table = buildIdxTable(reader);

  Hit hit;
  EXPECT_FALSE(StarDictIndex::findKey(reader, table, StarDictIndex::IDX_RECORD_BYTES, "blueberry", hit));
  EXPECT_FALSE(StarDictIndex::findKey(reader, table, StarDictIndex::IDX_RECORD_BYTES, "aardvark", hit));
  EXPECT_FALSE(StarDictIndex::findKey(reader, table, StarDictIndex::IDX_RECORD_BYTES, "zucchini", hit));
  // Case folding is ASCII-only, and it applies to lookups too.
  EXPECT_TRUE(StarDictIndex::findKey(reader, table, StarDictIndex::IDX_RECORD_BYTES, "Banana", hit));
  EXPECT_EQ(hit.key, "banana");
}

TEST(StarDictIndexFindKey, WorksOverARealFile) {
  TempFileReader reader(makeIdx(numberedKeys(MANY)));
  CheckpointTable table;
  ASSERT_TRUE(StarDictIndex::buildCheckpoints(reader, StarDictIndex::IDX_RECORD_BYTES, table));

  Hit hit;
  ASSERT_TRUE(StarDictIndex::findKey(reader, table, StarDictIndex::IDX_RECORD_BYTES, "k01500", hit));
  EXPECT_EQ(StarDictIndex::readBE32(hit.record), 1500u * 100u);
}

TEST(StarDictIndexKeyAtOrdinal, ResolvesBoundariesAndRejectsOverrun) {
  MemoryReader reader(makeIdx(numberedKeys(MANY)));
  const CheckpointTable table = buildIdxTable(reader);

  EXPECT_EQ(StarDictIndex::keyAtOrdinal(reader, table, StarDictIndex::IDX_RECORD_BYTES, 0), "k00000");
  EXPECT_EQ(StarDictIndex::keyAtOrdinal(reader, table, StarDictIndex::IDX_RECORD_BYTES, 1023), "k01023");
  EXPECT_EQ(StarDictIndex::keyAtOrdinal(reader, table, StarDictIndex::IDX_RECORD_BYTES, 1024), "k01024");
  EXPECT_EQ(StarDictIndex::keyAtOrdinal(reader, table, StarDictIndex::IDX_RECORD_BYTES, 2599), "k02599");
  EXPECT_EQ(StarDictIndex::keyAtOrdinal(reader, table, StarDictIndex::IDX_RECORD_BYTES, 9999), "");
}

// The regression this whole file exists for: an ordinal is four raw bytes, and
// every ordinal below 0x01000000 contains a NUL. Any scheme that resynchronises
// on "the next NUL byte" reads those as key terminators and loses the entry.
TEST(StarDictIndexFindSynonym, ResolvesOrdinalsThatContainNulBytes) {
  const std::vector<std::pair<std::string, uint32_t>> entries = {
      {"aaa", 0x00000000}, {"bbb", 0x0000000D}, {"ccc", 0x00000100}, {"ddd", 0x00010000},
      {"eee", 0x00FFFFFF}, {"fff", 0x01020304}, {"ggg", 0x0000006B},  // 0x6B is 'k': looks like text
      {"hhh", 0x00006B6B}, {"iii", 0x00000041},
  };
  MemoryReader reader(makeSyn(entries));
  CheckpointTable table;
  ASSERT_TRUE(StarDictIndex::buildCheckpoints(reader, StarDictIndex::SYN_RECORD_BYTES, table));
  EXPECT_EQ(table.totalEntries, entries.size());

  for (const auto& entry : entries) {
    uint32_t ordinal = 0xFFFFFFFF;
    ASSERT_TRUE(StarDictIndex::findSynonym(reader, table, entry.first, ordinal)) << entry.first;
    EXPECT_EQ(ordinal, entry.second) << entry.first;
  }

  uint32_t ordinal = 0;
  EXPECT_FALSE(StarDictIndex::findSynonym(reader, table, "zzz", ordinal));
}

TEST(StarDictIndexFindSynonym, ResolvesEveryKeyAcrossCheckpointBoundaries) {
  std::vector<std::pair<std::string, uint32_t>> entries;
  entries.reserve(MANY);
  for (const std::string& key : numberedKeys(MANY)) {
    // Small ordinals on purpose: this is the shape the real dictionaries have,
    // and the one the old resync lost most of.
    entries.emplace_back(key, static_cast<uint32_t>(entries.size()) + 3);
  }
  MemoryReader reader(makeSyn(entries));
  CheckpointTable table;
  ASSERT_TRUE(StarDictIndex::buildCheckpoints(reader, StarDictIndex::SYN_RECORD_BYTES, table));

  size_t missed = 0;
  for (const auto& entry : entries) {
    uint32_t ordinal = 0;
    if (!StarDictIndex::findSynonym(reader, table, entry.first, ordinal) || ordinal != entry.second) ++missed;
  }
  EXPECT_EQ(missed, 0u);
}

TEST(StarDictIndexNeighbourhood, StartsBeforeTheAnchorAndBoundsTheWalk) {
  MemoryReader reader(makeIdx(numberedKeys(MANY)));
  const CheckpointTable table = buildIdxTable(reader);

  // Anchored in the last segment: two segments either side clamps to the file.
  const uint32_t total = StarDictIndex::seekToNeighbourhood(reader, table, "k02500", 2);
  EXPECT_EQ(reader.position(), 0u);
  EXPECT_EQ(total, MANY);

  // The walk starts at a key, not mid-record.
  EXPECT_EQ(StarDictIndex::readKey(reader), "k00000");
}

}  // namespace
