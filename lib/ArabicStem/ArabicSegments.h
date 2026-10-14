#pragma once

// How a word was cut up to reach the candidate that answered -- the display side
// of ArabicStem::fallbackCandidates().
//
// Layer 1 of a lookup is this: عَرَضَهُمۡ is عرض plus the pronoun هم. Layer 2 is
// the dictionary probe on عرض. Showing layer 1 is what tells a reader (and us)
// which of the two got it wrong when the answer looks strange.
//
// The label vocabulary is the one D:\Projects\arabicle uses in its bottom sheet:
// Arabic grammatical names, not English part-of-speech tags. Same strings on the
// device and in tools/dictprobe, because both link this file.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ArabicStem.h"

namespace ArabicStem {

struct Segment {
  enum Kind : uint8_t { Core, Affix };
  std::string text;
  Kind kind = Core;
  // Arabic grammatical name, e.g. "ضمير مضاف إليه". Static storage; never null.
  const char* label = "";
};

// A word never carries more than: two proclitics, a verb prefix, the stem, and
// one enclitic. Two spare, because the proclitic splitter can divide a stacked
// prefix the stripper took in one bite.
constexpr size_t MAX_SEGMENTS = 6;

// Writes the reading-order segmentation of `cleanedWord` implied by `candidate`
// and returns how many it wrote. Always at least one (the stem alone) -- a
// candidate that was built rather than cut has no affixes to show.
// How a candidate was arrived at. This is as close to a confidence as the stemmer
// can honestly get: there is no score behind it, only the kind of evidence each
// reading rests on, which is what a reader needs in order to disbelieve one.
//
// Not a ranking. The order lookup() probes in is the ranking, and it weighs things
// this cannot see -- how common a shape is, what the word before it governs. Two
// readings of the same standing can be worlds apart in likelihood.
enum class Standing : uint8_t {
  AsWritten,  // the word itself, marks folded away: nothing was removed
  Proved,     // a mark in the word licensed the cut -- the wasl alef, the shadda, the sukun
  Cut,        // affixes the tables know came off; every letter left is the word's own
  Respelled,  // a cut and a spelling repair on top: the taa marbuta, a hamza seat, a plural waw
  Rebuilt,    // the measure rules read the marks and rebuilt a stem that was never written
  Guessed,    // a spelling swap with nothing behind it
};

Standing standing(const std::string& word, const Candidate& candidate);

// One short phrase, for the screen that asks the reader which reading was meant.
const char* standingLabel(Standing standing);

size_t segments(const std::string& cleanedWord, const Candidate& candidate, Segment* out, size_t cap);

// "و + عرض + هم"
std::string joinSegments(const Segment* segments, size_t count);

}  // namespace ArabicStem
