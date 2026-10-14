#pragma once

// Word normalization and lookup-candidate generation for the dictionary.
//
// Pure string work: no I/O, no ESP dependencies, so the code the device runs is
// the code the host test suite and tools/dictprobe run. DictionaryStore stays
// the device-side owner of the SD, cache and UI concerns; everything here is a
// function of its argument alone.

#include <cstdint>
#include <string>
#include <vector>

namespace ArabicStem {

// Which respelling of the word a candidate was cut from. The affixes are not
// stored as text: the display slices them back out of this base, so a candidate
// costs four bytes over the string it already carried.
enum class Base : uint8_t {
  Folded,         // marks stripped, hamza folded -- the ordinary path
  Geminate,       // shadda kept, for the doubled-measure keys
  Detanweened,    // accusative tanween's alef carrier cut off first
  DaggerSpelled,  // superscript alef written out as the letter it stands for
  HollowAlef,     // a hollow verb's dropped alef put back: قلنا read as قالنا
  HollowJussive,  // the same alef, dropped by jazm rather than by an ending: لم يقل
  Reconstructed,  // built rather than cut: the measure rules read the marks and rebuilt a stem
  Guessed,        // built with nothing behind it: a spelling swap, a dropped tanween, a bare stem
};

// A lookup candidate and the cut that produced it. `form` is what gets probed --
// byte-identical to what fallbackForms() has always returned, in the same order.
struct Candidate {
  std::string form;
  Base base = Base::Folded;
  uint8_t prefixBytes = 0;      // proclitics off the front of the base (stacked: وال is 4)
  uint8_t verbPrefixBytes = 0;  // imperfect/future prefix, after the proclitics
  uint8_t suffixBytes = 0;      // the last enclitic off the end of the base
  uint8_t subjectBytes = 0;     // a subject marker between the stem and it: خلق + نا + كم
  bool fromVerb = false;        // the stem came from a verb prefix: object pronoun, not possessive
  bool wasl = false;            // the front alef is hamzat al-wasl: an imperative, not a measure IV
  bool imperative = false;      // the stem is a command, however its front hamza is written
  bool vocative = false;        // the yaa in front is حرف النداء, not the imperfect's prefix
  bool tawkid = false;          // the nun that came off is نون التوكيد, not a dual's or نون النسوة
};

// Trims edge punctuation (ASCII, plus the multi-byte set books actually use),
// removes interior soft hyphens, and lowercases Latin. Diacritics survive:
// fallbackForms() needs them to tell one Arabic measure from another.
std::string cleanWord(const std::string& word);

// What the word BEFORE the selected one proves about it. Arabic writes a word's
// part of speech into its neighbour as often as into its own letters: أَلَمۡ أَقُلۡ
// is لم + أقل, and لم governs a مضارع مجزوم, so أقل is the verb أقول with the
// waw its jazm dropped -- never the elative أقل that the same three letters also
// spell, and that the index answers with when nothing says otherwise.
enum class Neighbour : uint8_t {
  None,  // nothing before the word, or a word that governs nothing
  Jazm,  // لم: what follows is a مضارع مجزوم, and a hollow verb writes it contracted
};

// What the preceding token governs. Takes it as the book wrote it -- marks and
// hamza seats fold here, the same way a query's do.
Neighbour governs(const std::string& previousWord);

// Ordered lookup candidates for a word the index did not hold as written. The
// caller probes them in order and stops at the first hit, so earlier means "more
// likely to be the headword actually wanted". `spanish` enables the Spanish-only
// suffix rules; an Arabic word returns before any Latin rule runs.
std::vector<Candidate> fallbackCandidates(const std::string& cleanedWord, bool spanish,
                                          Neighbour before = Neighbour::None);

// The forms alone, in the same order: what a caller that only probes wants.
std::vector<std::string> fallbackForms(const std::string& cleanedWord, bool spanish,
                                       Neighbour before = Neighbour::None);

// The respelling of `cleanedWord` a candidate's affix offsets are measured
// against. ArabicSegments slices this; nothing else needs it.
std::string baseText(const std::string& cleanedWord, Base base);

// How many bytes at the front of `word` the lookup keeps: everything before the
// first punctuation that ends a word wherever it stands (an ornate paren, a
// bracket, an Arabic comma). A mushaf glues the ayah's citation straight onto the
// last word, and the layout hands the pair over as one token; the selector uses
// this to highlight the word rather than the citation with it.
size_t wordLookupLength(const std::string& word);

// The form a fuzzy suggestion scan anchors on. Deliberately less stripped than
// fallbackForms()' last entry -- see the definition.
std::string suggestionAnchor(const std::string& word);

// Latin case folding, shared with the definition formatter and the suggestion
// scorer.
std::string lowercaseLatinUtf8(const std::string& input);
std::string foldLatinUtf8ToAscii(const std::string& input);

}  // namespace ArabicStem
