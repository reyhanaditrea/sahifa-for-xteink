#pragma once

// StarDict index and synonym probing, over an abstract byte reader.
//
// Both files have the same shape -- a byte-sorted run of NUL-terminated keys,
// each followed by a fixed-size record (8 bytes of offset+size in the .idx, 4
// bytes of ordinal in the .syn) -- so one set of routines serves both, with the
// record size as a parameter.
//
// The Reader indirection is what lets the device, the host tests, and
// tools/dictprobe run the same code: the device backend wraps HalFile (and so
// keeps every access behind storageMutex), the host backends wrap a FILE* or a
// byte vector. It is a virtual interface rather than a template so the whole
// body stays in one translation unit -- one copy in flash, no per-backend
// instantiation -- and no std::function is involved.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace StarDictIndex {

// One checkpoint every this many entries. Lookup binary-searches the
// checkpoints, then walks at most this many entries linearly.
constexpr uint32_t CHECKPOINT_INTERVAL = 1024;
constexpr size_t MAX_CHECKPOINT_COUNT = 4096;
constexpr size_t IDX_RECORD_BYTES = 8;
constexpr size_t SYN_RECORD_BYTES = 4;
// Headwords longer than this are truncated on read; the stream still drains to
// the terminator so the record after it stays aligned.
constexpr size_t MAX_KEY_BYTES = 255;

class Reader {
 public:
  virtual ~Reader() = default;
  virtual bool seekSet(size_t pos) = 0;
  // Bytes actually read, or negative on error.
  virtual int read(void* buf, size_t bytes) = 0;
  // One byte, or negative at EOF/error.
  virtual int read() = 0;
  virtual size_t position() const = 0;
  virtual size_t fileSize() = 0;
};

// Escape hatches for the two things the device needs during a long scan and no
// other backend has: feeding the watchdog, and refusing to grow a table the
// heap cannot hold. Null members mean "not applicable", which is the host case.
struct ScanHooks {
  void (*yield)() = nullptr;
  bool (*hasHeadroom)(size_t bytes) = nullptr;
  void (*progress)(void* ctx, int percent) = nullptr;
  void* progressCtx = nullptr;
};

struct CheckpointTable {
  std::vector<uint32_t> offsets;   // file position of every CHECKPOINT_INTERVAL-th key
  std::vector<uint32_t> ordinals;  // entry ordinal at that position
  uint32_t totalEntries = 0;
  uint32_t fileSize = 0;

  bool empty() const { return offsets.empty() || offsets.size() != ordinals.size(); }
  void clear() {
    offsets.clear();
    ordinals.clear();
    totalEntries = 0;
    fileSize = 0;
  }
};

struct Hit {
  std::string key;
  uint8_t record[IDX_RECORD_BYTES] = {};
};

uint32_t readBE32(const uint8_t* buf);
// Reads one NUL-terminated key, leaving the stream on the byte after the
// terminator so the caller can read the record that follows.
std::string readKey(Reader& file);
// Byte order with ASCII case folding -- the order StarDict files are sorted in.
int compareKeys(const std::string& lhs, const std::string& rhs);

// Walks the whole file once, recording a checkpoint every CHECKPOINT_INTERVAL
// entries. False only when nothing usable was built.
bool buildCheckpoints(Reader& file, size_t recordBytes, CheckpointTable& out, const ScanHooks& hooks = {});

bool findKey(Reader& file, const CheckpointTable& table, size_t recordBytes, const std::string& word, Hit& hit);
std::string keyAtOrdinal(Reader& file, const CheckpointTable& table, size_t recordBytes, uint32_t ordinal);
// Resolves an alternate spelling to the ordinal of its headword in the .idx.
bool findSynonym(Reader& syn, const CheckpointTable& synTable, const std::string& word, uint32_t& ordinal);

// Positions `file` at the start of the checkpoint segment before `anchor` and
// returns how many entries the caller may walk from there. Used by the fuzzy
// suggestion scan, which wants a neighbourhood rather than an exact match.
uint32_t seekToNeighbourhood(Reader& file, const CheckpointTable& table, const std::string& anchor,
                             int segmentsEitherSide);

}  // namespace StarDictIndex
