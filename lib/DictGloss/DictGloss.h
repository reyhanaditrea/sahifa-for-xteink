#pragma once

#include <string>

// The one plain sentence an article can be reduced to.
//
// A dictionary entry answers a question the reader did not ask first: before the
// morphology, the authorities and the twelve senses, they want to know what the
// word means. Layer 1 of the lookup shows that sentence and nothing else, and
// this is where it is found.
//
// The rules are Lane's Lexicon's, because that is the article this reader mostly
// opens: senses are bullets, editorial insertions are bracketed, attributions are
// parenthesised or hoisted to [n], and a rendering is never inside any of them.
// Any other dictionary falls through to the opening sentence, which is the
// nearest thing an entry without that structure has.
//
// Pure string work: no SD, no allocator games beyond std::string, host-testable.
namespace DictGloss {

// Longest gloss returned, in codepoints. Past this the text is cut at the last
// clause boundary and an ellipsis is added.
constexpr size_t GLOSS_MAX = 170;

// The nearest thing to a meaning in `definitionText`, or "" when the article
// holds nothing that reads as one. Input is the definition as the store hands it
// over -- tags already stripped, senses already one per line.
std::string gloss(const std::string& definitionText);

}  // namespace DictGloss
