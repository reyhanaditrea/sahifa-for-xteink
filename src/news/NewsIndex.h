#pragma once

#include <cstdint>

// Fixed-size records so the reader can seek straight to item N without holding
// the whole feed in RAM. Always memcpy in and out: the ESP32-C3 faults on
// unaligned multi-byte loads, and these come straight off an SD read buffer.
namespace NewsIndex {

inline constexpr uint32_t MAGIC = 0x5357454E;  // "NEWS"
// 2: records gained the item epoch, and a slot now accumulates across syncs.
// 3: records gained the article URL and the fetched full text.
inline constexpr uint16_t VERSION = 3;

// Longest title we keep, in bytes. Titles are cut on a UTF-8 boundary.
inline constexpr size_t TITLE_BYTES = 96;

// Items kept per feed. detik ships 100 per fetch and the Guardian 45; 40 is
// well past what fits in three days of reading and caps the parse cost.
inline constexpr size_t MAX_ITEMS_PER_FEED = 40;

// Days the app keeps: today, yesterday, the day before.
inline constexpr size_t DAY_COUNT = 3;

// Longest article URL we store. Every link across the built-in outlets fits in
// about a hundred bytes; a longer one simply gets no full-text option.
inline constexpr size_t URL_BYTES = 128;

// Record flags.
inline constexpr uint8_t FLAG_QUEUED = 1 << 0;  // fetch the full text at the next sync
inline constexpr uint8_t FLAG_FAILED = 1 << 1;  // a fetch was tried and came back empty

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t syncOrdinal;  // local day ordinal the sync ran on
};

struct Record {
  char title[TITLE_BYTES];
  uint32_t epoch;       // 0 when the feed gave no usable date
  uint32_t dayOrdinal;  // local day the item belongs to; 0 when unknown
  uint32_t bodyOffset;  // byte offset into feedN.txt
  uint32_t bodyLen;
  char url[URL_BYTES];  // the item's own page; empty when the feed gave none
  // The article text fetched from that page, appended to the same feedN.txt.
  // fullLen 0 means it was never fetched, and the summary is what we show.
  uint32_t fullOffset;
  uint32_t fullLen;
  uint8_t flags;
  uint8_t reserved[3];  // keeps the record 4-byte aligned on disk
};

inline constexpr size_t HEADER_BYTES = sizeof(Header);
inline constexpr size_t RECORD_BYTES = sizeof(Record);
static_assert(sizeof(Header) == 12, "index header layout is on-disk format");
static_assert(sizeof(Record) == TITLE_BYTES + URL_BYTES + 28, "index record layout is on-disk format");

}  // namespace NewsIndex
