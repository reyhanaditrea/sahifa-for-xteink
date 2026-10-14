#include "news/NewsIndexIo.h"

#include <HalStorage.h>
#include <Logging.h>

#include "news/NewsPaths.h"

namespace NewsIndexIo {

namespace {

size_t recordOffset(const uint16_t itemIndex) {
  return NewsIndex::HEADER_BYTES + static_cast<size_t>(itemIndex) * NewsIndex::RECORD_BYTES;
}

bool readHeader(HalFile& index, NewsIndex::Header& header) {
  if (index.read(&header, sizeof(header)) != static_cast<int>(sizeof(header))) return false;
  return header.magic == NewsIndex::MAGIC && header.version == NewsIndex::VERSION;
}

}  // namespace

bool readRecord(const size_t slot, const uint16_t itemIndex, NewsIndex::Record& record) {
  char indexPath[64];
  NewsPaths::indexPath(slot, indexPath, sizeof(indexPath));
  HalFile index;
  if (!Storage.openFileForRead("NEWS", indexPath, index)) return false;

  NewsIndex::Header header{};
  if (!readHeader(index, header) || itemIndex >= header.count) return false;
  if (!index.seek(recordOffset(itemIndex))) return false;
  // memcpy through the caller's struct: the bytes come straight off an SD read
  // and the C3 faults on unaligned wide loads.
  return index.read(&record, sizeof(record)) == static_cast<int>(sizeof(record));
}

bool writeRecord(const size_t slot, const uint16_t itemIndex, const NewsIndex::Record& record) {
  char indexPath[64];
  NewsPaths::indexPath(slot, indexPath, sizeof(indexPath));
  // O_RDWR, never openFileForWrite: that one adds O_TRUNC, which would empty
  // the whole index to update one record inside it.
  HalFile index = Storage.open(indexPath, O_RDWR);
  if (!index) return false;
  if (!index.seek(recordOffset(itemIndex))) {
    index.close();
    return false;
  }
  const bool ok = index.write(&record, sizeof(record)) == sizeof(record);
  index.close();
  if (!ok) LOG_ERR("NEWS", "slot %zu item %u: record write failed", slot, itemIndex);
  return ok;
}

bool updateFlags(const size_t slot, const uint16_t itemIndex, const uint8_t set, const uint8_t clear) {
  NewsIndex::Record record{};
  if (!readRecord(slot, itemIndex, record)) return false;
  record.flags = static_cast<uint8_t>((record.flags & ~clear) | set);
  return writeRecord(slot, itemIndex, record);
}

uint16_t itemCount(const size_t slot) {
  char indexPath[64];
  NewsPaths::indexPath(slot, indexPath, sizeof(indexPath));
  HalFile index;
  if (!Storage.openFileForRead("NEWS", indexPath, index)) return 0;
  NewsIndex::Header header{};
  if (!readHeader(index, header)) return 0;
  return header.count;
}

}  // namespace NewsIndexIo
