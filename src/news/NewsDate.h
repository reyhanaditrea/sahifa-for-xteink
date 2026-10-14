#pragma once

#include <cstdint>

// Feed timestamp parsing. RSS ships RFC 822 dates ("Mon, 25 Aug 2026 13:05:00
// +0300"), Atom and RSS 1.0/RDF ship RFC 3339 ("2026-08-25T13:05:00Z"), and
// hand-rolled feeds ship either with sloppy spacing. Everything here is pure so
// it can be unit-tested on the host; the caller supplies the local UTC offset
// rather than reaching for SETTINGS.
namespace NewsDate {

// Seconds since the Unix epoch, or 0 when the string is not a date we know.
uint32_t parseFeedDateToEpoch(const char* s);

// Local day ordinal (days since 1970-01-01 in UTC+localOffsetSec) for a feed
// date string. 0 means "unknown" -- callers keep such items rather than drop
// them, since a missing date is far more common than a wrong one.
uint32_t parseFeedDateToLocalOrdinal(const char* s, int32_t localOffsetSec);

// Local day ordinal for an epoch; 0 when the epoch predates 2020 (clock never
// synced). Shared with the activities so the sync and the UI agree on "today".
uint32_t localOrdinalForEpoch(uint32_t epochSeconds, int32_t localOffsetSec);

}  // namespace NewsDate
