#pragma once

// Arabic morphology for lookup: reading the diacritics a book prints, and
// rebuilding the headword a conjugated verb was formed from.
//
// The rest of the fallback chain only ever removes letters, which is enough for
// nouns but not for verbs. A derived measure is written with a letter the
// imperfect does not carry -- yastakhlifu is listed under istakhlafa, yanTaliqu
// under inTalaqa -- so reaching those headwords means putting a letter back, and
// knowing which one means reading the vowel on the subject prefix. That vowel is
// exactly what stripping the marks throws away, so this works on the word as
// written, before any of that happens.

#include <cstddef>
#include <cstdint>
#include <string>

namespace ArabicStem {
namespace morph {

enum Vowel : uint8_t {
  V_NONE,
  V_A,   // fatha
  V_U,   // damma
  V_I,   // kasra
  V_AN,  // fathatan
  V_UN,  // dammatan
  V_IN,  // kasratan
};

// One letter with whatever marks were written on it. A word is scanned into
// these once, and every rule below reads units rather than bytes.
struct Unit {
  uint16_t cp = 0;
  Vowel vowel = V_NONE;
  bool shadda = false;
  bool sukun = false;
};

// Longer than any word a rule here can classify; a longer input is not Arabic
// morphology this can speak to, and the scanner abstains rather than truncate.
constexpr size_t MAX_UNITS = 24;
// Enough for the widest rule (a measure that emits three spellings) plus the
// detanweened form.
constexpr size_t MAX_MEASURE_FORMS = 4;
constexpr size_t MAX_STEM_GUESSES = 3;

// Splits `s` into letters-with-marks, folding the hamza-seat alefs the way the
// rest of the chain does. Returns 0 -- abstain -- for anything that is not
// wholly Arabic letters and marks, or that is longer than `cap`.
size_t scanUnits(const std::string& s, Unit* out, size_t cap);

// Appends one letter, with its shadda if it carries one.
void appendUnit(std::string& out, uint16_t cp, bool shadda = false);

// Units taken by a proclitic at the head of `u`, or 0 if there is none to take.
// The proclitic's own vowel rides on its unit and goes with it.
size_t procliticUnits(const Unit* u, size_t n);

// The headwords the marks on this word point at, best first. Empty when the
// word carries no vowel on its subject prefix -- that mark is the evidence, and
// without it a guess here would outrank the plain stem for no reason.
size_t measureCandidates(const Unit* u, size_t n, std::string* out, size_t cap);

// The same reconstructions for an unvowelled stem, where there is no evidence
// and these are guesses. The caller ranks them behind everything derived from
// the word as written.
size_t stemGuesses(const std::string& bareStem, std::string* out, size_t cap);

// Exchanges a FINAL alef maqsura and yaa. Dictionary keys disagree about which
// one a final yaa is written with; a medial one is never in doubt, so swapping
// those only ever produced probes that could not match.
std::string swapFinalMaqsura(const std::string& s);

}  // namespace morph
}  // namespace ArabicStem
