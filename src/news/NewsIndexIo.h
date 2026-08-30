#pragma once

#include <cstddef>
#include <cstdint>

#include "news/NewsIndex.h"

// Single-record reads and writes against a feed's index file.
//
// The reader, the full-text fetch and the sync all need to touch one record in
// place -- to read its URL, to flag it for the next sync, to store where its
// article text landed -- so the seek arithmetic and the O_RDWR rule (never
// openFileForWrite, which truncates) live here rather than in three activities.
namespace NewsIndexIo {

bool readRecord(size_t slot, uint16_t itemIndex, NewsIndex::Record& record);
bool writeRecord(size_t slot, uint16_t itemIndex, const NewsIndex::Record& record);

// Reads, applies set/clear to record.flags, writes back. False when the record
// could not be read or written.
bool updateFlags(size_t slot, uint16_t itemIndex, uint8_t set, uint8_t clear);

// Number of records in a slot's index; 0 when it is missing or stale.
uint16_t itemCount(size_t slot);

}  // namespace NewsIndexIo
