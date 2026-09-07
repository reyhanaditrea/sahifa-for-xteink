#include "DictGloss.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace DictGloss {
namespace {

constexpr const char* BULLET = "\xE2\x80\xA2";  // U+2022

size_t utf8Length(const unsigned char lead) {
  if ((lead & 0x80) == 0) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

uint32_t codepointAt(const std::string& text, const size_t pos) {
  const unsigned char lead = static_cast<unsigned char>(text[pos]);
  const size_t len = utf8Length(lead);
  if (len == 1) return lead;
  uint32_t cp = lead & (0xFF >> (len + 1));
  for (size_t i = 1; i < len && pos + i < text.size(); ++i) {
    cp = (cp << 6) | (static_cast<unsigned char>(text[pos + i]) & 0x3F);
  }
  return cp;
}

// A letter of any script, which is what decides where a word begins and ends.
// Only ASCII would make "al-Kisāʾī" three words and let the search for a rendering
// start in the middle of a name -- the vowel and the hamza carriers are letters,
// and Lane's transliterations are full of them. A dash or a dot is not one, so a
// sense that opens straight after an em dash is still found.
bool isWordChar(const std::string& text, const size_t pos) {
  const uint32_t cp = codepointAt(text, pos);
  if (cp < 0x80) return std::isalnum(static_cast<int>(cp)) != 0 || cp == '_';
  return (cp >= 0x00C0 && cp <= 0x024F) ||  // Latin-1 letters, Latin Extended-A/B
         (cp >= 0x02B0 && cp <= 0x02FF) ||  // modifier letters: the ʾ and ʿ of a transliteration
         (cp >= 0x0300 && cp <= 0x036F) ||  // combining marks
         (cp >= 0x0370 && cp <= 0x04FF) ||  // Greek, Cyrillic
         (cp >= 0x0590 && cp <= 0x05FF) ||  // Hebrew
         (cp >= 0x1E00 && cp <= 0x1FFF) ||  // Latin Extended Additional: the ṣ and ḥ of a transliteration

         (cp >= 0x0600 && cp <= 0x06FF) ||  // Arabic, its marks included
         (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
         (cp >= 0xFE70 && cp <= 0xFEFF);
}

// The byte before `pos`, as the codepoint it belongs to.
bool wordCharBefore(const std::string& text, const size_t pos) {
  if (pos == 0) return false;
  size_t start = pos - 1;
  while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) --start;
  return isWordChar(text, start);
}

bool isLower(const char c) { return c >= 'a' && c <= 'z'; }
bool isUpper(const char c) { return c >= 'A' && c <= 'Z'; }

// Whether text[pos] starts a whole word rather than the tail of a longer one.
bool atWordStart(const std::string& text, const size_t pos) { return !wordCharBefore(text, pos); }

bool wordEndsAt(const std::string& text, const size_t pos) { return pos >= text.size() || !isWordChar(text, pos); }

// Bracket depth at every index. Lane's editorial insertions and his parenthesised
// attributions are asides -- a rendering is never inside one -- so the search for
// a meaning only ever looks at depth 0.
std::vector<int> depths(const std::string& text) {
  std::vector<int> out;
  out.reserve(text.size());
  int depth = 0;
  for (const char ch : text) {
    if (ch == '[' || ch == '(') ++depth;
    out.push_back(depth);
    if (ch == ']' || ch == ')') depth = std::max(0, depth - 1);
  }
  return out;
}

size_t codepointCount(const std::string& text) {
  size_t count = 0;
  for (const char c : text) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
  }
  return count;
}

// Byte offset of codepoint `index`, or text.size() when the string is shorter.
size_t byteOfCodepoint(const std::string& text, const size_t index) {
  size_t count = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
    if (count == index) return i;
    ++count;
  }
  return text.size();
}

// Up to the full stop that ends the rendering. Lane writes "i. e." and "e. g."
// with stops in them, so a stop followed by a lower-case letter ends nothing.
std::string cut(const std::string& text) {
  const std::vector<int> depth = depths(text);
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '.' || depth[i]) continue;
    // Only the next two characters are consulted, and only a lower-case letter
    // among them says the stop was an abbreviation's. Two spaces after a stop are
    // the end of a sense whatever follows them, which is how "mice.  see ١ in
    // that art." stops at the meaning instead of running into the cross-reference.
    std::string tail;
    size_t scan = i + 1;
    for (size_t taken = 0; taken < 2 && scan < text.size(); ++taken) {
      const size_t len = std::min(utf8Length(static_cast<unsigned char>(text[scan])), text.size() - scan);
      tail += text.substr(scan, len);
      scan += len;
    }
    size_t head = 0;
    while (head < tail.size() && std::isspace(static_cast<unsigned char>(tail[head]))) ++head;
    size_t back = tail.size();
    while (back > head && std::isspace(static_cast<unsigned char>(tail[back - 1]))) --back;
    if (head < back && isLower(tail[head])) continue;
    return text.substr(0, i + 1);
  }
  return text;
}

// The punctuation a sense is left hanging off when it is cut out of a longer
// line: spaces, dashes of both kinds, and the separators between senses.
bool isTrimmable(const std::string& text, size_t pos, size_t& width) {
  static const char* const TRIM[] = {" ",
                                     "\t",
                                     "\n",
                                     "-",
                                     ",",
                                     ";",
                                     ":",
                                     "\xE2\x80\x94",  // em dash
                                     "\xE2\x80\x93",  // en dash
                                     "\xC2\xB7"};     // middle dot
  for (const char* mark : TRIM) {
    const size_t len = std::strlen(mark);
    if (text.compare(pos, len, mark) == 0) {
      width = len;
      return true;
    }
  }
  return false;
}

std::string trimMarks(const std::string& text) {
  size_t begin = 0, end = text.size();
  size_t width = 0;
  while (begin < end && isTrimmable(text, begin, width)) begin += width;
  while (end > begin) {
    // Walk back to the start of the last codepoint, then ask about it.
    size_t last = end - 1;
    while (last > begin && (static_cast<unsigned char>(text[last]) & 0xC0) == 0x80) --last;
    if (!isTrimmable(text, last, width) || last + width != end) break;
    end = last;
  }
  return text.substr(begin, end - begin);
}

std::string trim(const std::string& raw) {
  const std::string text = trimMarks(raw);
  if (codepointCount(text) <= GLOSS_MAX) return text;
  const std::string head = text.substr(0, byteOfCodepoint(text, GLOSS_MAX));
  const size_t semi = head.rfind(';');
  const size_t comma = head.rfind(',');
  size_t stop = std::string::npos;
  if (semi != std::string::npos) stop = semi;
  if (comma != std::string::npos && (stop == std::string::npos || comma > stop)) stop = comma;
  // A clause boundary in the first half is not worth cutting to -- it would throw
  // away more of the meaning than the truncation itself does.
  if (stop != std::string::npos && codepointCount(head.substr(0, stop)) > GLOSS_MAX / 2) {
    return head.substr(0, stop) + " \xE2\x80\xA6";
  }
  const size_t space = head.rfind(' ');
  return (space == std::string::npos ? head : head.substr(0, space)) + " \xE2\x80\xA6";
}

size_t wordCount(const std::string& text) {
  size_t words = 0;
  bool inWord = false;
  for (const char c : text) {
    const bool space = std::isspace(static_cast<unsigned char>(c)) != 0;
    if (!space && !inWord) ++words;
    inWord = !space;
  }
  return words;
}

// A construction note, not a meaning: "With سمع [as transitive by itself]".
bool isNotAGloss(const std::string& text) {
  static const char* const OPENERS[] = {"With", "See", "Compare"};
  for (const char* opener : OPENERS) {
    const size_t len = std::strlen(opener);
    if (text.compare(0, len, opener) == 0 && wordEndsAt(text, len)) return true;
  }
  return false;
}

// Where a rendering is likely to start, in order of how surely the run is a
// rendering rather than a remark about one.
enum class Opener { Subject, Article, Capitalised };

bool matchesOpener(const std::string& text, const size_t pos, const Opener opener) {
  if (!atWordStart(text, pos)) return false;
  static const char* const SUBJECTS[] = {"He", "It", "She", "They", "One", "We", "Thou"};
  static const char* const ARTICLES[] = {"A", "An", "The", "Such", "Any"};
  if (opener == Opener::Capitalised) {
    // \b[A-Z][a-z]{2,}\b
    if (!isUpper(text[pos])) return false;
    size_t end = pos + 1;
    while (end < text.size() && isLower(text[end])) ++end;
    return end - pos >= 3 && wordEndsAt(text, end);
  }
  const char* const* words = opener == Opener::Subject ? SUBJECTS : ARTICLES;
  const size_t count =
      opener == Opener::Subject ? sizeof(SUBJECTS) / sizeof(*SUBJECTS) : sizeof(ARTICLES) / sizeof(*ARTICLES);
  for (size_t i = 0; i < count; ++i) {
    const size_t len = std::strlen(words[i]);
    if (text.compare(pos, len, words[i]) == 0 && wordEndsAt(text, pos + len)) return true;
  }
  return false;
}

// "[3]" -- an authority hoisted out of the sense it was attached to. Not part of
// the meaning, and it lands in the middle of one often enough to matter.
std::string stripCitations(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '[') {
      size_t j = i + 1;
      while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
      if (j > i + 1 && j < text.size() && text[j] == ']') {
        i = j + 1;
        continue;
      }
    }
    out.push_back(text[i]);
    ++i;
  }
  return out;
}

std::vector<std::string> bulletsOf(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  const size_t bulletLen = std::strlen(BULLET);
  while (start <= text.size()) {
    const size_t nl = text.find('\n', start);
    const std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    size_t first = 0;
    while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first]))) ++first;
    if (line.compare(first, bulletLen, BULLET) == 0) {
      std::string body = line.substr(first + bulletLen);
      size_t begin = 0;
      while (begin < body.size() && std::isspace(static_cast<unsigned char>(body[begin]))) ++begin;
      size_t end = body.size();
      while (end > begin && std::isspace(static_cast<unsigned char>(body[end - 1]))) --end;
      out.push_back(body.substr(begin, end - begin));
    }
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return out;
}

// The house format: senses as bullets, the first one that says what the word
// means rather than how it is constructed.
std::string houseGloss(const std::string& text) {
  const std::vector<std::string> bullets = bulletsOf(text);
  for (const std::string& bullet : bullets) {
    const std::string body = stripCitations(bullet);
    const std::vector<int> depth = depths(body);
    for (const Opener opener : {Opener::Subject, Opener::Article, Opener::Capitalised}) {
      for (size_t i = 0; i < body.size(); ++i) {
        if (!matchesOpener(body, i, opener)) continue;
        // Inside a bracket it is an aside about the sense, not the sense.
        if (depth[i]) continue;
        const std::string piece = trim(cut(body.substr(i)));
        if (wordCount(piece) >= 3 && !isNotAGloss(piece)) return piece;
        // The first thing that looked like an opening and was not one settles
        // this pattern: a second guess further into the same sense reads worse
        // than moving on to a weaker pattern from the top.
        break;
      }
    }
  }
  // An article with no rendering in it at all -- a Sihah or Arramooz one, which
  // is Arabic throughout. Its first claim is still the nearest thing it holds.
  return bullets.empty() ? std::string() : trim(cut(stripCitations(bullets[0])));
}

std::string collapseSpaces(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  bool pendingSpace = false;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) out.push_back(' ');
    pendingSpace = false;
    out.push_back(c);
  }
  return out;
}

// A leading run of /…/ groups, however many the entry lists. Slashes around a
// phonetic run is near-universal, and an entry that opens with four of them --
// /haʊs/, /huːs/, /hæʊs/, /hɐʊs/ -- would otherwise spend the whole gloss saying
// how the word sounds.
std::string stripPronunciation(const std::string& text) {
  size_t pos = 0;
  while (true) {
    size_t scan = pos;
    while (scan < text.size() && std::isspace(static_cast<unsigned char>(text[scan]))) ++scan;
    if (scan >= text.size() || text[scan] != '/') break;
    const size_t close = text.find('/', scan + 1);
    if (close == std::string::npos || close - scan - 1 > 40) break;
    scan = close + 1;
    while (scan < text.size() && std::isspace(static_cast<unsigned char>(text[scan]))) ++scan;
    if (scan < text.size() && (text[scan] == ',' || text[scan] == ';')) ++scan;
    pos = scan;
  }
  return text.substr(pos);
}

}  // namespace

std::string gloss(const std::string& definitionText) {
  const size_t bulletLen = std::strlen(BULLET);
  for (size_t start = 0; start <= definitionText.size();) {
    const size_t nl = definitionText.find('\n', start);
    size_t first = start;
    while (first < definitionText.size() && first != nl &&
           std::isspace(static_cast<unsigned char>(definitionText[first]))) {
      ++first;
    }
    if (definitionText.compare(first, bulletLen, BULLET) == 0) return houseGloss(definitionText);
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  // No bullets, no notes, no Lane. Any other dictionary puts the meaning first and
  // the discussion after it, so the opening sentence is the nearest thing to a
  // gloss it has -- and the brief showing that beats the brief showing nothing.
  std::string flat = stripPronunciation(collapseSpaces(definitionText));
  size_t begin = 0;
  while (begin < flat.size() && (flat[begin] == ' ' || flat[begin] == ',' || flat[begin] == ';')) ++begin;
  size_t end = flat.size();
  while (end > begin && (flat[end - 1] == ' ' || flat[end - 1] == ',' || flat[end - 1] == ';')) --end;
  return trim(cut(flat.substr(begin, end - begin)));
}

}  // namespace DictGloss
