#include "ArabicStem.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>

#include "ArabicMorph.h"

namespace ArabicStem {
namespace {

// --- Arabic lookup normalization (Foulad eInk addition) ---
// StarDict lookup is exact-match; Arabic words in books carry tashkeel,
// tatweel, hamza-seat variants, and proclitics (ال، و، ب...) that dictionary
// headwords usually lack (FreeDict ara-eng headwords are bare). These helpers
// generate bare fallback forms. UTF-8 walker: Arabic block chars are 2-byte
// sequences (0xD8/0xD9 lead).

bool containsArabicUtf8(const std::string& s) {
  for (size_t i = 0; i + 1 < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xD8 || c == 0xD9 || c == 0xDA || c == 0xDB) return true;
  }
  return false;
}

// Removes tashkeel (U+064B-U+065F), tatweel (U+0640), and dagger alef (U+0670).
//
// keepShadda spares U+0651 alone. Every other mark is a vowel a writer may or
// may not have written, so stripping them is what makes a headword findable;
// the shadda is not one of those. It doubles a consonant, and that is a
// different word: عَلَّمَ (measure II, "he taught") against the root عَلِمَ ("he
// knew"). Folding it away is what forced every doubled measure to answer with
// its whole root's merged article instead of its own.
std::string stripArabicMarks(const std::string& s, const bool keepShadda = false) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xD9 && i + 1 < s.size()) {
      const unsigned char n = static_cast<unsigned char>(s[i + 1]);
      // U+0640 tatweel = D9 80; U+064B..U+065F marks = D9 8B..D9 9F; U+0670 = D9 B0
      // U+0651 shadda = D9 91, inside that mark range.
      if ((n == 0x80 || (n >= 0x8B && n <= 0x9F) || n == 0xB0) && !(keepShadda && n == 0x91)) {
        i += 2;
        continue;
      }
    }
    // U+06D6..U+06ED = DB 96..DB AD: the Quranic annotation signs. A vowelled
    // mushaf writes its sukun as U+06E1, not the U+0652 above, so without this a
    // word quoted from one keeps a mark on every consonant and matches no
    // headword at all -- the whole of an ayah misses.
    if (c == 0xDB && i + 1 < s.size()) {
      const unsigned char n = static_cast<unsigned char>(s[i + 1]);
      if (n >= 0x96 && n <= 0xAD) {
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

// Folds hamza-seat variants of alef to bare alef: أ(D8A3) إ(D8A5) آ(D8A2) ٱ(D9B1) -> ا(D8A7)
//
// Also the two-character spelling of the same sound: a mushaf writes Adam as
// hamza + alef (D8A1 D8A7) where ordinary text writes the single madda alef, and
// the pair has to collapse the same way or no Quranic quotation of the name ever
// reaches its headword.
std::string normalizeArabicHamza(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const unsigned char n = (i + 1 < s.size()) ? static_cast<unsigned char>(s[i + 1]) : 0;
    if (c == 0xD8 && n == 0xA1 && i + 3 < s.size() && static_cast<unsigned char>(s[i + 2]) == 0xD8 &&
        static_cast<unsigned char>(s[i + 3]) == 0xA7) {
      out.push_back(static_cast<char>(0xD8));
      out.push_back(static_cast<char>(0xA7));
      i += 4;
      continue;
    }
    if ((c == 0xD8 && (n == 0xA2 || n == 0xA3 || n == 0xA5)) || (c == 0xD9 && n == 0xB1)) {
      out.push_back(static_cast<char>(0xD8));
      out.push_back(static_cast<char>(0xA7));
      i += 2;
      continue;
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

// Cuts accusative tanween's epenthetic alif carrier -- alef(D8A7) + fathatan(D98B)
// -- off the end of the word, but only while the fathatan itself is still there to
// prove it: ...alan names no root letter, it is a case marker written on a bare
// alef that stripArabicMarks() alone would leave indistinguishable from a genuine
// final-alef root (da'a). An undiacriticized word never carries this mark, so this
// never fires on one -- it can only recover what the diacritic itself reveals.
// The two orders occur: a text may place the fathatan on the alef itself, or on
// the letter before it with the alef trailing, and both are the same ending.
std::string stripArabicTanweenAlif(const std::string& s) {
  if (s.size() < 4) return s;
  const unsigned char a = static_cast<unsigned char>(s[s.size() - 4]);
  const unsigned char b = static_cast<unsigned char>(s[s.size() - 3]);
  const unsigned char c = static_cast<unsigned char>(s[s.size() - 2]);
  const unsigned char d = static_cast<unsigned char>(s[s.size() - 1]);
  const bool alefThenMark = a == 0xD8 && b == 0xA7 && c == 0xD9 && d == 0x8B;
  const bool markThenAlef = a == 0xD9 && b == 0x8B && c == 0xD8 && d == 0xA7;
  if (alefThenMark) return s.substr(0, s.size() - 4);
  // Only the alef goes; the fathatan stays for stripArabicMarks to take, so a
  // word this misreads still has its mark to prove what it was.
  if (markThenAlef) return s.substr(0, s.size() - 2);
  return s;
}

// The superscript alef is a long a written as a mark: "subhaana" is spelled with
// one where ordinary text writes the letter, and stripArabicMarks() drops it with
// the rest. That is right for a demonstrative -- haadhaa is keyed "hdha", not
// "haadha" -- so this is offered as another spelling rather than substituted
// outright, and it is asked for after the form that drops the mark.
// The words Arabic files under the spelling WITHOUT the alef, however the mushaf
// prints them. Everywhere else a superscript alef stands in for a letter the word
// really has -- سَمَٰوَٰتِ is سماوات, صَلۡصَٰلٖ is صلصال -- and writing it out reaches the
// article. For these it does the opposite: إِلَٰهٍ is filed as اله, and الاه is a
// different word that the index also holds, so the written-out reading would
// answer and answer wrongly. A closed list, because the class is closed: this is
// the ألف خنجرية of the demonstratives, of الله and its attributes, and of لكن.
bool filedWithoutItsAlef(const std::string& folded) {
  static constexpr const char* WORDS[] = {"\xD8\xA7\xD9\x84\xD9\x84\xD9\x87",
                                          "\xD8\xA7\xD9\x84\xD9\x87",
                                          "\xD9\x87\xD8\xB0\xD8\xA7",
                                          "\xD9\x87\xD8\xB0\xD9\x87",
                                          "\xD8\xB0\xD9\x84\xD9\x83",
                                          "\xD9\x87\xD8\xA4\xD9\x84\xD8\xA7\xD8\xA1",
                                          "\xD8\xA7\xD9\x88\xD9\x84\xD8\xA6\xD9\x83",
                                          "\xD9\x84\xD9\x83\xD9\x86",
                                          "\xD8\xB7\xD9\x87",
                                          "\xD9\x87\xD9\x83\xD8\xB0\xD8\xA7"};
  static constexpr const char* PROCLITICS[] = {"\xD9\x88",
                                               "\xD9\x81",
                                               "\xD8\xA8",
                                               "\xD9\x83",
                                               "\xD9\x84",
                                               "\xD8\xA7\xD9\x84",
                                               "\xD9\x88\xD8\xA7\xD9\x84",
                                               "\xD9\x81\xD8\xA7\xD9\x84",
                                               "\xD8\xA8\xD8\xA7\xD9\x84",
                                               "\xD9\x83\xD8\xA7\xD9\x84",
                                               "\xD9\x84\xD9\x84"};
  for (const char* word : WORDS) {
    if (folded == word) return true;
    for (const char* proclitic : PROCLITICS) {
      const size_t n = std::strlen(proclitic);
      if (folded.size() > n && folded.compare(0, n, proclitic) == 0 &&
          folded.compare(n, std::string::npos, word) == 0) {
        return true;
      }
    }
  }
  return false;
}

std::string spellDaggerAlifAsAlef(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (static_cast<unsigned char>(s[i]) == 0xD9 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xB0) {
      out.push_back(static_cast<char>(0xD8));
      out.push_back(static_cast<char>(0xA7));
      i += 2;
      continue;
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

// Proclitics that attach to the front of an Arabic word, longest first so "wal-" is
// taken whole rather than read as "wa-" + "al-". Shared by the two callers on purpose:
// they take different slices of the SAME list, and one copy is what stops the slices
// drifting apart.
//
// The first MULTI_LETTER_PROCLITICS entries are unambiguous -- no Arabic word opens
// with those sequences as its root -- so they are safe anywhere. The rest are single
// letters that are also ordinary first root letters, and are only safe where a wrong
// strip is cheap. See getSuggestionAnchor() for where that line falls.
constexpr const char* ARABIC_PROCLITICS[] = {
    "\xD9\x88\xD8\xA7\xD9\x84",  // wal = وال
    "\xD9\x81\xD8\xA7\xD9\x84",  // fal = فال
    "\xD8\xA8\xD8\xA7\xD9\x84",  // bal = بال
    "\xD9\x83\xD8\xA7\xD9\x84",  // kal = كال
    "\xD9\x84\xD9\x84",          // lil = لل
    "\xD8\xA7\xD9\x84",          // al = ال
    "\xD9\x88",                  // wa = و
    "\xD9\x81",                  // fa = ف
    "\xD8\xA8",                  // bi = ب
    "\xD9\x83",                  // ka = ك
    "\xD9\x84",                  // li = ل
};
constexpr size_t MULTI_LETTER_PROCLITICS = 6;

// A stripped stem has to be able to BE a word. Arabic roots are triliteral, and Arabic
// is two bytes a letter in UTF-8, so anything below this is debris rather than a
// candidate: "كتاب" (book) would otherwise lose its first root letter to the "ك"
// proclitic and offer up a two-letter remainder.
constexpr size_t MIN_ARABIC_STEM_BYTES = 6;

// Imperfect verb prefixes. The last of these is alef because normalizeArabicHamza()
// has already folded the first-person hamza onto it.
//
// Every entry here is tried independently against the same stem, not chained, so
// the four Form X (istaf3ala) entries below are just more literal prefixes, not a
// second pass -- each fires alongside, never instead of, a shorter one that also
// matches. Form X stacks its person marker directly on the measure's own "ist-",
// so peeling only "ya-"/"ta-"/"na-"/"a-" leaves "ist-" still attached and the
// measure's root never surfaces (yastaf-dilu never reaches FDL). But "-st-" is
// also just three ordinary root letters (yastur-ha, he covers her, root S-T-R), so
// these four are placed LAST: list order is the order lookup() sees the resulting
// forms in, and the double strip must only get to answer for a stem the shorter,
// safer guess above it couldn't.
//
// File scope (not local to getFallbackForms(), where every other prefix/suffix
// table lives) because the geminate-key probe needs it too: a doubled measure's
// imperfect always carries one of these, unlike its past tense, which carries
// none on the 3rd-person-masculine form the geminate key already matches as-is.
constexpr const char* VERB_PREFIXES[] = {
    "\xD8\xB3\xD9\x8A",          // sa-ya
    "\xD8\xB3\xD8\xAA",          // sa-ta
    "\xD8\xB3\xD9\x86",          // sa-na
    "\xD9\x8A",                  // ya
    "\xD8\xAA",                  // ta
    "\xD9\x86",                  // na
    "\xD8\xA7",                  // a (folded from hamza)
    "\xD9\x8A\xD8\xB3\xD8\xAA",  // ya-st- (Form X imperfect, 3rd m.)
    "\xD8\xAA\xD8\xB3\xD8\xAA",  // ta-st- (Form X imperfect, 3rd f. / 2nd)
    "\xD9\x86\xD8\xB3\xD8\xAA",  // na-st- (Form X imperfect, 1st pl.)
    "\xD8\xA7\xD8\xB3\xD8\xAA",  // a-st- (Form X past, folded hamza)
};

bool isAsciiTrimChar(unsigned char c) { return c < 0x80 && !std::isalnum(c); }

bool matchTokenAt(const std::string& text, const size_t pos, const char* token) {
  const size_t len = strlen(token);
  return pos + len <= text.size() && text.compare(pos, len, token) == 0;
}

// Punctuation that cleanWord() peels off the ends of a selected word. ASCII is
// handled by isAsciiTrimChar() -- every non-alphanumeric byte under 0x80, so a
// trailing "," or "?" on an English word never reaches here. This list is the
// multi-byte half, and it has to carry the Arabic marks separately: a word tapped
// out of an Arabic page ends in U+060C or U+061F, not in the ASCII comma and
// question mark that look the same, and an untrimmed one turns every lookup at
// the end of a clause into a miss.
constexpr const char* TRIM_TOKENS[] = {
    "\xC2\xA1",      // inverted !
    "\xC2\xAB",      // <<
    "\xC2\xBB",      // >>
    "\xC2\xBF",      // inverted ?
    "\xD8\x8C",      // Arabic comma
    "\xD8\x9B",      // Arabic semicolon
    "\xD8\x9F",      // Arabic question mark
    "\xDB\x94",      // Arabic full stop
    "\xE2\x80\x90",  // hyphen
    "\xE2\x80\x91",  // non-breaking hyphen
    "\xE2\x80\x92",  // figure dash
    "\xE2\x80\x93",  // en dash
    "\xE2\x80\x94",  // em dash
    "\xE2\x80\x95",  // horizontal bar
    "\xE2\x80\x98",  // left single quote
    "\xE2\x80\x99",  // right single quote
    "\xE2\x80\x9C",  // left double quote
    "\xE2\x80\x9D",  // right double quote
    "\xE2\x80\xA6",  // ellipsis
    // The ornate parentheses a mushaf sets its ayat in, and the invisible marks
    // that ride along with quoted scripture. Both attach to the word rather than
    // standing apart, so an untrimmed one makes every quotation's first and last
    // word a miss.
    "\xEF\xB4\xBE",  // ornate left parenthesis
    "\xEF\xB4\xBF",  // ornate right parenthesis
    "\xD8\x9C",      // Arabic letter mark
    "\xE2\x80\x8B",  // zero-width space
    "\xE2\x80\x8C",  // zero-width non-joiner
    "\xE2\x80\x8D",  // zero-width joiner
    "\xE2\x80\x8E",  // left-to-right mark
    "\xE2\x80\x8F",  // right-to-left mark
};

// Punctuation that ENDS a word wherever it stands, not only at the ends. A mushaf
// closes its ayah and opens its citation with no space between them --
// "ٱلصَّٰغِرِينَ﴾[الأعراف: ١٠-١٣]" is one whitespace-delimited token, and the layout
// hands it over whole -- so trimming the outer edges still leaves the citation
// glued to the word and every such lookup misses. The word is what stands before
// the first of these.
constexpr const char* CUT_TOKENS[] = {
    "\xEF\xB4\xBE", "\xEF\xB4\xBF", "[",        "]",        "(", ")", "{", "}", "<", ">", "|",
    "\xD8\x8C",     "\xD8\x9B",     "\xD8\x9F", "\xDB\x94",
};

bool isCutTokenAt(const std::string& text, const size_t pos, size_t& tokenLen) {
  for (const char* token : CUT_TOKENS) {
    if (matchTokenAt(text, pos, token)) {
      tokenLen = strlen(token);
      return true;
    }
  }
  return false;
}

bool isUtf8TrimPrefix(const std::string& text, const size_t pos, size_t& tokenLen) {
  for (const char* token : TRIM_TOKENS) {
    if (matchTokenAt(text, pos, token)) {
      tokenLen = strlen(token);
      return true;
    }
  }
  return false;
}

bool isUtf8TrimSuffix(const std::string& text, const size_t end, size_t& tokenLen) {
  for (const char* token : TRIM_TOKENS) {
    const size_t len = strlen(token);
    if (end >= len && text.compare(end - len, len, token) == 0) {
      tokenLen = len;
      return true;
    }
  }
  return false;
}

}  // namespace

std::string lowercaseLatinUtf8(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(std::tolower(c)));
      continue;
    }
    if (c == 0xC3 && i + 1 < input.size()) {
      const unsigned char n = static_cast<unsigned char>(input[i + 1]);
      if (n >= 0x80 && n <= 0x96 && n != 0x97) {
        out.push_back(static_cast<char>(0xC3));
        out.push_back(static_cast<char>(n + 0x20));
        ++i;
        continue;
      }
      if (n == 0x9C) {
        out.push_back(static_cast<char>(0xC3));
        out.push_back(static_cast<char>(0xBC));
        ++i;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

std::string foldLatinUtf8ToAscii(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool lastSpace = true;
  for (size_t i = 0; i < input.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    char folded = '\0';
    if (c < 0x80) {
      if (std::isalnum(c)) {
        folded = static_cast<char>(std::tolower(c));
      } else if (std::isspace(c) || c == '-' || c == '/' || c == '.') {
        if (!lastSpace) {
          out.push_back(' ');
          lastSpace = true;
        }
        continue;
      } else {
        continue;
      }
    } else if (c == 0xC3 && i + 1 < input.size()) {
      const unsigned char n = static_cast<unsigned char>(input[i + 1]);
      switch (n) {
        case 0x81:
        case 0xA1:
        case 0x80:
        case 0xA0:
          folded = 'a';
          break;
        case 0x89:
        case 0xA9:
        case 0x88:
        case 0xA8:
          folded = 'e';
          break;
        case 0x8D:
        case 0xAD:
        case 0x8C:
        case 0xAC:
          folded = 'i';
          break;
        case 0x93:
        case 0xB3:
        case 0x92:
        case 0xB2:
          folded = 'o';
          break;
        case 0x9A:
        case 0xBA:
        case 0x9C:
        case 0xBC:
          folded = 'u';
          break;
        case 0x91:
        case 0xB1:
          folded = 'n';
          break;
        case 0x87:
        case 0xA7:
          folded = 'c';
          break;
        default:
          break;
      }
      ++i;
    }

    if (folded != '\0') {
      out.push_back(folded);
      lastSpace = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

size_t wordLookupLength(const std::string& word) {
  for (size_t i = 0; i < word.size();) {
    size_t tokenLen = 0;
    if (i > 0 && isCutTokenAt(word, i, tokenLen)) return i;
    const unsigned char lead = static_cast<unsigned char>(word[i]);
    i += lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  }
  return word.size();
}

std::string cleanWord(const std::string& word) {
  if (word.empty()) return "";

  size_t start = 0;
  while (start < word.size()) {
    const unsigned char c = static_cast<unsigned char>(word[start]);
    if (c < 0x80 && isAsciiTrimChar(c)) {
      ++start;
      continue;
    }
    size_t tokenLen = 0;
    if (isUtf8TrimPrefix(word, start, tokenLen)) {
      start += tokenLen;
      continue;
    }
    break;
  }

  size_t end = word.size();
  while (end > start) {
    const unsigned char c = static_cast<unsigned char>(word[end - 1]);
    if (c < 0x80 && isAsciiTrimChar(c)) {
      --end;
      continue;
    }
    size_t tokenLen = 0;
    if (isUtf8TrimSuffix(word, end, tokenLen)) {
      end -= tokenLen;
      continue;
    }
    break;
  }
  if (start >= end) return "";

  std::string out = word.substr(start, end - start);
  for (size_t i = 0; i < out.size();) {
    size_t tokenLen = 0;
    if (i > 0 && isCutTokenAt(out, i, tokenLen)) {
      out.erase(i);
      break;
    }
    const unsigned char lead = static_cast<unsigned char>(out[i]);
    i += lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  }
  for (size_t i = 0; i + 1 < out.size();) {
    if (static_cast<unsigned char>(out[i]) == 0xC2 && static_cast<unsigned char>(out[i + 1]) == 0xAD) {
      out.erase(i, 2);
    } else {
      ++i;
    }
  }
  return lowercaseLatinUtf8(out);
}

// findSuggestions() scans the index neighbourhood around one word and scores every
// key against it, so this single choice decides which part of the dictionary the
// user is offered. It used to be getFallbackForms().back() -- the most heavily
// stripped form available -- which is exactly the wrong pick, because the most
// stripped form is also the most likely to be wrong.
//
// The single-letter proclitics are the problem. "ك" (like), "ب" (with), "ل" (for)
// and "و" (and) are real prefixes, but they are also ordinary first root letters:
// "كتاب" (book) is not "ك" + "تاب", and nothing short of a lexicon can tell the
// two apart. As a LOOKUP candidate a wrong strip is cheap -- one index probe that
// misses, and the next form is tried. As the suggestion ANCHOR it is not: the scan
// lands in the wrong part of the index and every candidate offered is unrelated.
//
// So the anchor only uses transformations that cannot corrupt a word: dropping
// marks, folding hamza seats, and removing the multi-letter proclitics, which are
// unambiguous because no Arabic word begins with "ال" as root letters.
std::string suggestionAnchor(const std::string& word) {
  std::string anchor = normalizeArabicHamza(stripArabicMarks(word));
  // The multi-letter head of the shared table only. Looped because these do stack
  // ("و" + "بال" + stem), and one pass would leave the inner one in place.
  for (bool stripped = true; stripped;) {
    stripped = false;
    for (size_t i = 0; i < MULTI_LETTER_PROCLITICS; ++i) {
      const char* proclitic = ARABIC_PROCLITICS[i];
      const size_t n = strlen(proclitic);
      // Same three-letter floor as the fallback forms: never strip a word to debris.
      if (anchor.size() < n + MIN_ARABIC_STEM_BYTES || !anchor.starts_with(proclitic)) continue;
      anchor = anchor.substr(n);
      stripped = true;
      break;
    }
  }
  return anchor;
}

// The subject endings a perfect verb takes, longest first. They are the ones that
// begin with a consonant, which is exactly why a hollow verb contracts in front of
// them: قَالَ + نا cannot keep its long vowel and is written قُلْنَا.
namespace {
constexpr const char* HOLLOW_SUBJECT_ENDINGS[] = {
    "\xD8\xAA\xD9\x85\xD8\xA7",  // -tuma
    "\xD9\x86\xD8\xA7",          // -na
    "\xD8\xAA\xD9\x85",          // -tum
    "\xD8\xAA\xD9\x86",          // -tunna
    "\xD8\xAA",                  // -tu / -ta / -ti
    "\xD9\x86",                  // -na (3fp)
};

// Whether the word is VOWELLED as a contracted hollow perfect: the first radical
// carries a damma or a kasra and the one after it a sukun -- قُلْنَا, بِعْتُ. A
// sound triliteral in the same shape is written وَعَدْنَا, with a fatha where this
// wants a damma, so on vowelled text the two readings can be told apart -- which
// matters, because وقلنا and وعدنا are the same length and only one of them is a
// conjunction in front of a hollow verb.
//
// `stemStart` is a byte offset into the marks-stripped word, so the scan counts
// letters rather than bytes to reach it. Unvowelled text says nothing either way
// and answers false: the reading is still offered, just not ahead of the strips.
bool vowelledAsContractedHollow(const std::string& word, const size_t stemStart) {
  const size_t skipLetters = stemStart / 2;
  size_t letters = 0;
  bool sawMark = false;
  char firstRadicalMark = 0;
  char secondRadicalMark = 0;
  for (size_t i = 0; i + 1 < word.size(); i += 2) {
    const unsigned char lead = static_cast<unsigned char>(word[i]);
    const unsigned char next = static_cast<unsigned char>(word[i + 1]);
    const bool isMark = (lead == 0xD9 && ((next >= 0x8B && next <= 0x9F) || next == 0xB0)) ||
                        (lead == 0xDB && next >= 0x96 && next <= 0xAD);
    if (isMark) {
      sawMark = true;
      // U+06E1 is how a mushaf writes the sukun; treat it as one.
      const char mark = (lead == 0xDB && next == 0xA1) ? static_cast<char>(0x92) : static_cast<char>(next);
      if (letters == skipLetters + 1) firstRadicalMark = mark;
      if (letters == skipLetters + 2) secondRadicalMark = mark;
      continue;
    }
    ++letters;
  }
  if (!sawMark) return false;
  const bool shortVowel = firstRadicalMark == static_cast<char>(0x8F) ||  // damma
                          firstRadicalMark == static_cast<char>(0x90);    // kasra
  return shortVowel && secondRadicalMark == static_cast<char>(0x92);      // sukun
}

// لَأَمۡلَأَنَّ is لَ + أَمۡلَأُ + نَّ -- the oath's lam answered by نون التوكيد الثقيلة, a
// doubled nun written once under a shadda. Fold the marks away and one nun is
// left, which every ending table reads as something else: as the dual's ان, or,
// behind the alef the verb's own hamza folds to, as نون النسوة. Neither is ever
// written with a shadda, so the shadda is the proof, and it licenses exactly the
// strip the نون النسوة guard below otherwise refuses.
bool endsWithShaddaNun(const std::string& word) {
  size_t pos = word.size();
  bool shadda = false;
  while (pos >= 2) {
    const unsigned char lead = static_cast<unsigned char>(word[pos - 2]);
    const unsigned char next = static_cast<unsigned char>(word[pos - 1]);
    const bool mark = (lead == 0xD9 && ((next >= 0x8B && next <= 0x9F) || next == 0xB0)) ||
                      (lead == 0xDB && next >= 0x96 && next <= 0xAD);
    if (!mark) break;
    if (lead == 0xD9 && next == 0x91) shadda = true;
    pos -= 2;
  }
  return shadda && pos >= 2 && word.compare(pos - 2, 2, "\xD9\x86") == 0;
}

// ٱسۡجُدُواْ is سَجَدَ in the imperative, not the measure IV أَسْجَدَ that the same letters
// spell once the marks come off -- and the dictionary holds both, so whichever is
// probed first wins. The spelling settles it: hamzat al-wasl is written as U+0671,
// or as a bare alef under a kasra, and a measure IV perfect carries a fatha on its
// own hamza instead. Only when the word proves it does the stripped reading move up.
bool startsWithWaslAlef(const std::string& word) {
  if (word.size() < 4) return false;
  if (word.compare(0, 2, "\xD9\xB1") == 0) return true;
  if (word.compare(0, 2, "\xD8\xA7") != 0) return false;
  return word.compare(2, 2, "\xD9\x90") == 0;
}

// لِأٓدَمَ is لِ + آدَم, but folding the hamza away spells لادم -- which is a word of its
// own (the participle of لَدَمَ), so the whole-word reading hits an entry that has
// nothing to do with the text. The collision is one this file creates: where the
// original writes a hamza behind the proclitic, the folded whole word is a mirage
// and the stripped reading is the real one.
bool hamzaFollowsProclitic(const std::string& word) {
  static const char* PROCLITIC_LETTERS[] = {"\xD9\x88", "\xD9\x81", "\xD8\xA8", "\xD9\x83", "\xD9\x84"};
  if (word.size() < 6) return false;
  bool proclitic = false;
  for (const char* letter : PROCLITIC_LETTERS) {
    if (word.compare(0, 2, letter) == 0) proclitic = true;
  }
  if (!proclitic) return false;
  auto markAt = [&word](const size_t at) {
    if (at + 1 >= word.size()) return false;
    const unsigned char lead = static_cast<unsigned char>(word[at]);
    const unsigned char next = static_cast<unsigned char>(word[at + 1]);
    return (lead == 0xD9 && ((next >= 0x8B && next <= 0x9F) || next == 0xB0)) ||
           (lead == 0xDB && next >= 0x96 && next <= 0xAD);
  };
  size_t pos = 2;
  while (markAt(pos)) pos += 2;
  for (const char* hamza : {"\xD8\xA3", "\xD8\xA5", "\xD8\xA2", "\xD9\xB1"}) {
    if (word.compare(pos, 2, hamza) == 0) return true;
  }
  return false;
}

// قلنا -> قالنا: the perfect as it is written before the ending contracted it,
// or empty when the word is not that shape. A hollow verb (فعل أجوف) carries its
// middle radical as an alef in the perfect -- قَالَ, كَانَ, بَاعَ -- and drops it the
// moment one of the endings above attaches. What is left in front of the ending is
// two letters, one under the floor every strip in this file observes, so the ending
// is never peeled and the word reaches the index whole.
//
// `stemStart` is where those two letters begin (past a conjunction, if the word
// carries one) and `endingBytes` how many bytes the ending takes, so the caller can
// report the same cut it made.
std::string restoreHollowAlef(const std::string& folded, size_t* stemStart, size_t* endingBytes) {
  // "wa-"/"fa-" ride on a perfect as often as not, and the two letters are behind it.
  size_t start = 0;
  if (folded.size() >= 2 && static_cast<unsigned char>(folded[0]) == 0xD9 &&
      (static_cast<unsigned char>(folded[1]) == 0x88 || static_cast<unsigned char>(folded[1]) == 0x81)) {
    start = 2;
  }
  // كَانَ + نا is written كُنَّا: the root's own noon and the ending's merge into one
  // letter, so a single letter is left in front of the ending rather than two. The
  // noon the shadda stands for is put back with the alef.
  if (folded.size() == start + 2 + 4 && folded.ends_with("\xD9\x86\xD8\xA7")) {
    *stemStart = start;
    *endingBytes = 4;
    return folded.substr(0, start + 2) + "\xD8\xA7\xD9\x86" + folded.substr(start + 2);
  }
  for (const char* ending : HOLLOW_SUBJECT_ENDINGS) {
    const size_t n = std::strlen(ending);
    // Exactly two letters between the conjunction and the ending: three is a sound
    // root that needs no help, one is not a word.
    if (folded.size() != start + 4 + n || !folded.ends_with(ending)) continue;
    *stemStart = start;
    *endingBytes = n;
    std::string restored = folded.substr(0, start + 2) + "\xD8\xA7" + folded.substr(start + 2);
    // A hamza takes a seat only because a vowel or a sukun puts it on one. شِئْتُمَا
    // seats it on a yaa because the contraction gave it a sukun; with the alef back
    // the word is شَاءَ and the hamza stands on the line. Same for سُؤْتُ -> سَاءَ.
    if (restored.compare(start + 4, 2, "\xD8\xA6") == 0 || restored.compare(start + 4, 2, "\xD8\xA4") == 0) {
      restored.replace(start + 4, 2, "\xD8\xA1");
    }
    return restored;
  }
  return std::string();
}

// يَـٰٓـَٔادَمُ is يا + آدم, and يَٰقَوۡمِ is يا + قوم. A mushaf writes حرف النداء as a yaa
// with its alef above it as a superscript, glued to the name by a tatweel; fold the
// marks away and what is left at the front of the word is a bare yaa, which every
// table in this file reads as حرف المضارعة. يَٰمُوسَىٰٓ came out as موسى the VERB, and
// يَٰقَوۡمِ came out worse -- the measure rules read it as يَقُومُ and answered قام.
//
// A yaa carrying a superscript alef is the vocative and nothing else: no verb
// prefix is ever written with one. That is the evidence, and it is as good as any
// in this file.
bool startsWithVocativeYaa(const std::string& word) {
  if (word.compare(0, 2, "\xD9\x8A") != 0) return false;
  for (size_t at = 2; at + 1 < word.size(); at += 2) {
    const unsigned char lead = static_cast<unsigned char>(word[at]);
    const unsigned char next = static_cast<unsigned char>(word[at + 1]);
    const bool mark = (lead == 0xD9 && ((next >= 0x8B && next <= 0x9F) || next == 0xB0)) ||
                      (lead == 0xDB && next >= 0x96 && next <= 0xAD);
    // The tatweel that glues the particle to the name is a joiner, not a letter:
    // يَـٰٓـَٔادَمُ has one on each side of the superscript alef.
    if (lead == 0xD9 && next == 0x80) continue;
    if (!mark) return false;
    // The superscript alef the particle's own alef is written as, or the maddah
    // that rides on it before a hamza.
    if (lead == 0xD9 && (next == 0xB0 || next == 0x93)) return true;
  }
  return false;
}

// أَنظِرۡنِيٓ: the imperative of أَنۡظَرَ, measure IV. Nothing comes off the front -- the
// hamza is part of the headword, unlike the wasl alef a measure I command takes --
// so the letters alone cannot tell the command from the perfect it is keyed under.
// The marks can: أَفۡعِلۡ puts a kasra on the second radical and leaves the third
// quiescent, where أَفۡعَلَ has a fatha on the one and a vowel on the other.
//
// Only the name of the stem rides on this. The headword is the same either way,
// which is why it is asked after the candidates are built rather than while the
// list is being ordered.
bool isMeasureFourImperative(const std::string& word) {
  const auto markAt = [&word](const size_t at) {
    if (at + 1 >= word.size()) return false;
    const unsigned char lead = static_cast<unsigned char>(word[at]);
    const unsigned char next = static_cast<unsigned char>(word[at + 1]);
    return (lead == 0xD9 && ((next >= 0x8B && next <= 0x9F) || next == 0xB0)) ||
           (lead == 0xDB && next >= 0x96 && next <= 0xAD);
  };
  if (word.compare(0, 2, "\xD8\xA3") != 0 && word.compare(0, 2, "\xD8\xA5") != 0) return false;
  size_t letters = 0;
  bool kasra = false;
  bool quiescent = false;
  for (size_t i = 0; i + 1 < word.size(); i += 2) {
    if (!markAt(i)) {
      ++letters;
      continue;
    }
    // The marks sitting on the letter just counted.
    if (letters == 3 && word.compare(i, 2, "\xD9\x90") == 0) kasra = true;
    if (letters == 4 && (word.compare(i, 2, "\xD9\x92") == 0 || word.compare(i, 2, "\xDB\xA1") == 0)) {
      quiescent = true;
    }
  }
  return kasra && quiescent && letters >= 4;
}

// Whether the taa near the end of the word is a verb's subject marker rather than
// a noun's taa marbuta. The two are the same letter in the same place -- كَلِمَتُهُ is
// كلمة + ه and خَلَقۡتَنِي is خلق + ت + ني -- and only the vowels tell them apart: a
// perfect stem is quiescent before its subject marker (خَلَقْتَ, أَبْلَغْتُ), and the
// third-person feminine's own taa is quiescent itself (جَآءَتۡهُمۡ), while a taa marbuta
// carries a case vowel and the letter in front of it never has a sukun.
//
// Unvowelled the question cannot be asked, and the answer is no: the noun is the
// commoner word, and it is the reading the taa marbuta restore already reaches.
bool taaIsSubjectMarker(const std::string& word) {
  const auto sukunAt = [&word](const size_t at) {
    return (word.compare(at, 2, "\xD9\x92") == 0) || (word.compare(at, 2, "\xDB\xA1") == 0);
  };
  const auto markAt = [&word](const size_t at) {
    if (at + 1 >= word.size()) return false;
    const unsigned char lead = static_cast<unsigned char>(word[at]);
    const unsigned char next = static_cast<unsigned char>(word[at + 1]);
    return (lead == 0xD9 && ((next >= 0x8B && next <= 0x9F) || next == 0xB0)) ||
           (lead == 0xDB && next >= 0x96 && next <= 0xAD);
  };
  size_t taa = std::string::npos;
  for (size_t i = 0; i + 1 < word.size(); i += 2) {
    if (word.compare(i, 2, "\xD8\xAA") == 0) taa = i;
  }
  if (taa == std::string::npos) return false;
  // On the taa itself: the third-person feminine ending is written quiescent.
  for (size_t at = taa + 2; markAt(at); at += 2) {
    if (sukunAt(at)) return true;
  }
  // Or on the letter in front of it, which is the perfect's last radical.
  for (size_t at = taa; at >= 2 && markAt(at - 2); at -= 2) {
    if (sukunAt(at - 2)) return true;
  }
  return false;
}

// The vowel the text put on the letter the contraction left in the middle, which
// is the only thing that says WHICH letter came out and from where. Three letters
// after a jazm particle are two different verbs written the same way: يَقُلۡ is قال
// with the waw taken out of the middle, and تَجِدۡ is وجد with the waw taken off the
// front. The vowel separates them where anything can.
//
//  damma  -- only a hollow verb: an assimilated one that keeps a damma keeps its waw too
//  kasra  -- the assimilated class's own vowel (يَعِدُ, يَجِدُ, يَقِفُ), so that reading leads
//  fatha  -- said by both, so what says it is the letters: an assimilated verb takes
//            a fatha only when a guttural forces one (وَضَعَ, وَقَعَ, وَهَبَ), while a
//            hollow one takes it whatever its letters are. A guttural among the two
//            that are left is therefore evidence for the assimilated reading -- not
//            proof, since خَافَ has one too. See gutturalStem() below.
char contractedVowel(const std::string& word) {
  size_t letters = 0;
  for (size_t i = 0; i + 1 < word.size(); i += 2) {
    const unsigned char lead = static_cast<unsigned char>(word[i]);
    const unsigned char next = static_cast<unsigned char>(word[i + 1]);
    const bool mark = (lead == 0xD9 && ((next >= 0x8B && next <= 0x9F) || next == 0xB0)) ||
                      (lead == 0xDB && next >= 0x96 && next <= 0xAD);
    if (!mark) {
      ++letters;
      continue;
    }
    // The marks sitting on the second letter -- the prefix is the first.
    if (letters != 2 || lead != 0xD9) continue;
    if (next == 0x8F) return 'u';
    if (next == 0x90) return 'i';
    if (next == 0x8E) return 'a';
  }
  return 0;
}

// The letters that pull a verb's imperfect vowel down to a fatha: ء ه ع ح غ خ.
// An assimilated verb has no other reason to take one -- the whole class is
// kasra by default -- so a guttural among the two letters a contraction left is
// the difference between reading يَضَعۡ as وضع and reading it as ضاع.
bool gutturalStem(const std::string& letters) {
  static constexpr const char* GUTTURALS[] = {
      "\xD8\xA1", "\xD9\x87", "\xD8\xB9", "\xD8\xAD", "\xD8\xBA", "\xD8\xAE",
  };
  for (size_t i = 0; i + 1 < letters.size(); i += 2) {
    for (const char* guttural : GUTTURALS) {
      if (letters.compare(i, 2, guttural) == 0) return true;
    }
  }
  return false;
}

// لَمۡ أَقُلۡ -> اقال, لَمۡ يَكُنۡ -> يكان: the jussive written the way the perfect writes
// it, or empty when the word is not that shape. Jazm contracts a hollow verb
// exactly as a subject ending does, only with nothing behind it to show for it --
// أَقُولُ loses its waw and what is left is the prefix and two letters, one under
// the floor every strip in this file observes.
//
// So the verb is unreachable, and worse than unreachable: أَقُلۡ folds to اقل, which
// is a headword of its own (the elative, "fewer"), and it answers. Nothing in the
// word decides between the two readings, which is why this one is offered only
// when the word before licenses it. `prefixBytes` is where the two letters begin,
// so the display can report the same cut.
std::string restoreHollowJussive(const std::string& folded, size_t* prefixBytes) {
  // The imperfect's four subject markers, and no more: a jussive is a مضارع, so
  // the future's سي and the measures' augments cannot stand in front of one that
  // has contracted to two letters.
  static constexpr const char* IMPERFECT_PREFIXES[] = {
      "\xD9\x8A",  // ya
      "\xD8\xAA",  // ta
      "\xD9\x86",  // na
      "\xD8\xA7",  // a, folded from the hamza of أَقُلۡ
  };
  // Three letters exactly: the marker and the two the contraction left.
  if (folded.size() != 6) return std::string();
  for (const char* prefix : IMPERFECT_PREFIXES) {
    if (!folded.starts_with(prefix)) continue;
    *prefixBytes = 2;
    return folded.substr(0, 4) + "\xD8\xA7" + folded.substr(4);
  }
  return std::string();
}

}  // namespace

std::string baseText(const std::string& word, const Base base) {
  switch (base) {
    case Base::Folded:
      return normalizeArabicHamza(stripArabicMarks(word));
    case Base::Geminate:
      return normalizeArabicHamza(stripArabicMarks(word, true));
    case Base::Detanweened:
      return normalizeArabicHamza(stripArabicMarks(stripArabicTanweenAlif(word)));
    case Base::DaggerSpelled:
      return normalizeArabicHamza(stripArabicMarks(spellDaggerAlifAsAlef(word)));
    case Base::HollowAlef: {
      size_t stemStart = 0;
      size_t endingBytes = 0;
      return restoreHollowAlef(normalizeArabicHamza(stripArabicMarks(word)), &stemStart, &endingBytes);
    }
    case Base::HollowJussive: {
      size_t prefixBytes = 0;
      return restoreHollowJussive(normalizeArabicHamza(stripArabicMarks(word)), &prefixBytes);
    }
    case Base::Reconstructed:
    case Base::Guessed:
      break;
  }
  // Built rather than cut, so there is no base to slice affixes out of.
  return std::string();
}

Neighbour governs(const std::string& previousWord) {
  // لِمَ ("why") is the preposition ل on the interrogative ما, and with its marks
  // taken off it is spelled exactly like لَمۡ -- it governs nothing, and the word
  // behind it is as often a perfect as anything else. The kasra is the whole of
  // the difference, and a vowelled text writes it: no jazm particle carries one.
  for (size_t i = 0; i + 4 <= previousWord.size(); i += 2) {
    if (previousWord.compare(i, 2, "\xD9\x84") == 0 && previousWord.compare(i + 2, 2, "\xD9\x90") == 0) {
      return Neighbour::None;
    }
  }
  const std::string folded = normalizeArabicHamza(stripArabicMarks(previousWord));
  // أَلَمۡ, أَوَلَمۡ, أَفَلَمۡ, وَلَمۡ, فَلَمۡ are all لم: the interrogative hamza and the two
  // conjunctions ride on the particle without touching what it governs. The hamza
  // has folded to an alef by here, which is why an alef is one of the three read
  // past. Never past the last two letters -- a bare conjunction governs nothing.
  size_t at = 0;
  for (bool rode = true; rode && folded.size() >= at + 6;) {
    rode = false;
    for (const char* rider : {"\xD8\xA7", "\xD9\x88", "\xD9\x81"}) {
      if (folded.compare(at, 2, rider) != 0) continue;
      at += 2;
      rode = true;
      break;
    }
  }
  // لم alone. لمّا is the other jussive particle, and it is left out on purpose:
  // stripped of its marks it is also لِمَا ("for what") and لَمَّا ("when"), neither
  // of which governs anything, and the misreadings would outnumber the readings.
  if (folded.size() == at + 4 && folded.compare(at, 4, "\xD9\x84\xD9\x85") == 0) return Neighbour::Jazm;
  return Neighbour::None;
}

std::vector<std::string> fallbackForms(const std::string& word, const bool spanish, const Neighbour before) {
  const std::vector<Candidate> candidates = fallbackCandidates(word, spanish, before);
  std::vector<std::string> forms;
  forms.reserve(candidates.size());
  for (const Candidate& candidate : candidates) forms.push_back(candidate.form);
  return forms;
}

std::vector<Candidate> fallbackCandidates(const std::string& word, const bool spanish, const Neighbour before) {
  std::vector<Candidate> forms;
  const std::string lower = lowercaseLatinUtf8(word);
  if (lower != word) forms.push_back(Candidate{lower});

  // The provenance of a stem the affix passes are about to work on. Carried
  // rather than recomputed: by the time a suffix comes off, the proclitic that
  // came off the front is three call levels away.
  struct Provenance {
    Base base = Base::Folded;
    uint8_t prefixBytes = 0;
    uint8_t verbPrefixBytes = 0;
    bool fromVerb = false;
    // Last, and set by name rather than by position: every Provenance in this
    // function is written as a brace list, and a field in front of fromVerb would
    // silently take the bool that belongs to it.
    uint8_t subjectBytes = 0;
  };

  auto add = [&forms](std::string value, const Provenance& provenance = {}, const uint8_t suffixBytes = 0) {
    if (value.size() < 3) return;
    for (const Candidate& candidate : forms) {
      if (candidate.form == value) return;
    }
    forms.push_back(Candidate{std::move(value), provenance.base, provenance.prefixBytes, provenance.verbPrefixBytes,
                              suffixBytes, provenance.subjectBytes, provenance.fromVerb});
  };
  // Built rather than cut: the display shows the form alone.
  const Provenance built{Base::Reconstructed, 0, 0, false};

  // Arabic branch (Foulad eInk): bare forms first (marks stripped, hamza
  // folded), then with common proclitics removed -- definite article and
  // single-letter conjunctions/prepositions (وال، بال، ال، و، ب، ك، ل...).
  // Fallbacks only run after the exact query missed, so an over-strip can at
  // worst surface a related headword instead of NotFound. Latin/Spanish
  // stemming below is meaningless for Arabic -- return early.
  if (containsArabicUtf8(word)) {
    // A selection can arrive as more than one word: "و علّم", where the book set
    // the conjunction off from the verb it belongs to. cleanWord() only trims the
    // ends of the query, so that space survives into it, and no index key can
    // contain one -- the lookup was dead before any rule below got to run.
    //
    // Page selection is per-word and can never produce this; a typed query can,
    // and does -- "و علّم" is what a reader types when the book spaced the
    // conjunction off the verb.
    //
    // Which token is meant is not fixed by position: the particle can lead
    // ("و علّم") or the object trail ("علّم الناس"). What is reliable is that a
    // particle is short and a content word is not, so tokens shorter than a
    // triliteral root go last while the rest keep the order they were typed in.
    // They still go in -- a query of "من" alone is a real word with a curated
    // entry -- they just stop outranking the word next to them. Capped, so a
    // pasted sentence cannot turn one miss into hundreds of SD reads.
    if (word.find_first_of(" \t\r\n") != std::string::npos) {
      constexpr size_t MAX_SELECTION_TOKENS = 4;
      std::vector<std::string> tokens;
      for (size_t i = 0; i < word.size();) {
        const size_t start = word.find_first_not_of(" \t\r\n", i);
        if (start == std::string::npos) break;
        const size_t end = word.find_first_of(" \t\r\n", start);
        tokens.push_back(word.substr(start, end == std::string::npos ? end : end - start));
        i = (end == std::string::npos) ? word.size() : end;
      }
      std::stable_partition(tokens.begin(), tokens.end(),
                            [](const std::string& t) { return t.size() >= MIN_ARABIC_STEM_BYTES; });
      if (tokens.size() > MAX_SELECTION_TOKENS) tokens.resize(MAX_SELECTION_TOKENS);
      // Every candidate here belongs to one token of a multi-word query, so its
      // affix offsets would be measured against a base the display does not have.
      // They go in as bare forms; a spaced query is typed, not selected, and the
      // split it would show is the split of a word the reader did not point at.
      for (const std::string& token : tokens) {
        add(token, built);
        for (const Candidate& candidate : fallbackCandidates(token, spanish)) add(candidate.form, built);
      }
      // Nothing below could have used the spaced string: every rule there is a
      // letter-level edit on a single word.
      return forms;
    }

    const std::string bare = stripArabicMarks(word);
    const std::string folded = normalizeArabicHamza(bare);

    // Ahead of the bare form, and only for a word that actually carries a
    // shadda: the same word with that one mark kept. The dictionary keys its
    // doubled headwords under this shape as well as under the bare skeleton
    // they share with their root, so عَلَّمَ answers with عَلَّمَ instead of with
    // the whole root article -- 29KB of it for علم, of which readDefinition()
    // reads the first 16KB, so the block wanted was not merely buried but
    // unreachable. Strictly the more specific answer, so it is probed first;
    // a word with no shadda leaves geminate == folded and this whole branch is
    // skipped, so no existing lookup changes.
    std::string geminateVerbStem;
    const std::string geminate = normalizeArabicHamza(stripArabicMarks(word, true));
    if (geminate != folded) {
      const Provenance geminateBase{Base::Geminate, 0, 0, false};
      add(geminate, geminateBase);
      // Running text writes the proclitics onto the word ("wa-'allama"), and
      // the general derivation below only ever sees the bare form, so the
      // shadda-bearing stem has to shed them here or it is never asked for.
      // Just the proclitics: the suffix and verb-prefix passes below are about
      // reaching a root from an inflected form, and the root is exactly what
      // this key exists to avoid falling back to.
      std::string stem = geminate;
      size_t geminatePrefixBytes = 0;
      for (bool changed = true; changed;) {
        changed = false;
        for (const char* proclitic : ARABIC_PROCLITICS) {
          const size_t n = strlen(proclitic);
          if (stem.size() < n + MIN_ARABIC_STEM_BYTES || !stem.starts_with(proclitic)) continue;
          stem = stem.substr(n);
          geminatePrefixBytes += n;
          add(stem, Provenance{Base::Geminate, static_cast<uint8_t>(geminatePrefixBytes), 0, false});
          changed = true;
          break;
        }
      }
      // A doubled measure conjugated in the imperfect always carries a subject
      // prefix (ya-/ta-/na-/a-) -- unlike the past tense just handled above, which
      // carries none on the 3rd-person-masculine form the bare geminate key
      // already matches. Without this, "يفسّد" never reaches "فسّد" at all: it
      // falls straight through to derive() below, which strips the prefix but
      // only off the bare (shadda-less) form, landing on the root "فسد" and
      // silently losing the shadda this whole branch exists to keep.
      for (const char* verbPrefix : VERB_PREFIXES) {
        const size_t n = strlen(verbPrefix);
        if (stem.size() >= n + MIN_ARABIC_STEM_BYTES && stem.starts_with(verbPrefix)) {
          add(stem.substr(n),
              Provenance{Base::Geminate, static_cast<uint8_t>(geminatePrefixBytes), static_cast<uint8_t>(n), true});
          geminateVerbStem = stem.substr(n);
        }
      }
    }

    // The word as written, in every spelling the marks allow, before anything is cut
    // off any of them. The one with its superscript alef written out leads: where
    // the index holds both, it is the more specific of the two -- صَلۡصَٰلٖ is صلصال, the
    // word, and صلصل, the root it is formed from, and the word is what was pointed
    // at. Where only the folded spelling is a key -- هَٰذَا, ذَٰلِكَ, ٱللَّٰه, whose articles
    // are filed without the letter -- the written-out form misses and the folded one
    // answers on the very next probe, so nothing is lost by asking in this order.
    const std::string spelledWhole = normalizeArabicHamza(stripArabicMarks(spellDaggerAlifAsAlef(word)));
    if (spelledWhole != folded && !filedWithoutItsAlef(folded)) {
      add(spelledWhole, Provenance{Base::DaggerSpelled, 0, 0, false});
    }
    if (bare != word) add(bare);
    if (folded != bare) add(folded);

    // T2, the only step that reads the diacritics rather than discarding them.
    // A derived measure is listed under a perfect the imperfect does not spell:
    // yustakhlifu belongs to istakhlafa, yanTaliqu to inTalaqa, and no amount of
    // stripping reaches either -- the letter has to be put back, and the vowel on
    // the subject prefix is what says which one. Ahead of the strips below,
    // because it is evidence and they are guesses; empty when the word carries no
    // marks, which leaves the rest of the chain exactly as it was.
    size_t measureFormCount = 0;
    {
      morph::Unit units[morph::MAX_UNITS];
      size_t unitCount = morph::scanUnits(word, units, morph::MAX_UNITS);
      std::string measured[morph::MAX_MEASURE_FORMS];
      size_t offset = 0;
      // Running text writes the proclitics onto the verb ("wa-yufsidu"), so they
      // come off first -- carrying their own vowel with them -- and the detector
      // is tried again at each level.
      while (unitCount > offset) {
        measureFormCount =
            morph::measureCandidates(units + offset, unitCount - offset, measured, morph::MAX_MEASURE_FORMS);
        if (measureFormCount > 0) break;
        const size_t proclitic = morph::procliticUnits(units + offset, unitCount - offset);
        if (proclitic == 0) break;
        offset += proclitic;
      }
      for (size_t i = 0; i < measureFormCount; ++i) add(measured[i], built);
    }

    // The tanween-stripped form belongs here too: its fathatan is a written mark
    // saying the alef is a case ending, not a root letter. Below, it only gets a
    // derivation pass of its own, behind whatever the undetanweened word made of
    // itself first -- for "kitaban" that is "taba" and "aba", either of which can
    // be a real key and answer ahead of "kitab".
    if (const std::string detanweenedEarly = stripArabicTanweenAlif(word); detanweenedEarly != word) {
      add(normalizeArabicHamza(stripArabicMarks(detanweenedEarly)), Provenance{Base::Detanweened, 0, 0, false});
      ++measureFormCount;
    }

    // Every form here costs one index probe plus one .syn probe on the miss path, so
    // the affix rules are capped rather than left to multiply. Prefixes and suffixes
    // combine (ya- ... -un on one verb), and without a ceiling a single long word
    // could generate dozens of probes for a lookup that is already failing.
    constexpr size_t MAX_ARABIC_FORMS = 16;
    // Counted from after the evidence-backed forms, so a vowelled word does not
    // spend the strip budget on the answers it already found.
    const size_t stripCeiling = MAX_ARABIC_FORMS + measureFormCount;
    auto arabicAdd = [&add, &forms, &word, stripCeiling](std::string value, const Provenance& provenance = {},
                                                         const uint8_t suffixBytes = 0) {
      if (forms.size() >= stripCeiling) return;
      // lookup() probes the query itself, through both the index and the .syn file,
      // before it touches this list. A form identical to it is two guaranteed-dead
      // SD reads, and the suffix pass below starts from the unmodified word, so it
      // produces one on every Arabic lookup that carries no marks.
      if (value == word) return;
      add(std::move(value), provenance, suffixBytes);
    };

    // The length test is the real work: starts_with/ends_with alone would happily
    // strip a word down to nothing.
    auto hasPrefix = [](const std::string& base, const char* affix, const size_t n) {
      return base.size() >= n + MIN_ARABIC_STEM_BYTES && base.starts_with(affix);
    };
    auto hasSuffix = [](const std::string& base, const char* affix, const size_t n) {
      return base.size() >= n + MIN_ARABIC_STEM_BYTES && base.ends_with(affix);
    };

    // Set once for the whole word: the strips below all work on respellings that
    // have already lost the shadda that proves it.
    const bool nunTawkid = endsWithShaddaNun(word);
    // Same: the sukun that says a taa is a subject marker is gone by the time the
    // strips see the word.
    const bool subjectTaa = taaIsSubjectMarker(word);

    // Attached pronouns, the perfect-tense person markers, then the plural/dual
    // endings. Longest first for the same reason: "-huma" must win over "-ha".
    // The perfect markers conjugate a past-tense verb for its subject ("dhahaba"
    // -> "dhahabtu", I went); for a sound triliteral Form I verb the bare past is
    // already the headword, so peeling one lands on it.
    static constexpr const char* ENCLITICS[] = {
        "\xD9\x87\xD9\x85\xD8\xA7",  // -huma
        "\xD9\x83\xD9\x85\xD8\xA7",  // -kuma
        "\xD8\xAA\xD9\x85\xD8\xA7",  // -tuma
        "\xD9\x87\xD9\x85",          // -hum
        "\xD9\x87\xD9\x86",          // -hunna
        "\xD9\x83\xD9\x85",          // -kum
        "\xD9\x83\xD9\x86",          // -kunna
        "\xD8\xAA\xD9\x85",          // -tum
        "\xD8\xAA\xD9\x86",          // -tunna
        "\xD9\x87\xD8\xA7",          // -ha
        "\xD9\x86\xD8\xA7",          // -na
        "\xD9\x86\xD9\x8A",          // -ni
        "\xD9\x88\xD9\x86",          // -un (pl)
        "\xD9\x8A\xD9\x86",          // -in (pl)
        "\xD8\xA7\xD9\x86",          // -an (dual)
        "\xD8\xA7\xD8\xAA",          // -at (pl)
        "\xD9\x88\xD8\xA7",          // -u (verb)
        "\xD8\xA9",                  // -a (feminine, taa marbuta)
        "\xD9\x87",                  // -hu
        "\xD9\x83",                  // -ka
        "\xD9\x8A",                  // -i
        "\xD8\xAA",                  // -tu / -ta / -ti
        "\xD9\x86",                  // -na (3fp)
    };
    // A perfect verb writes its subject and its object in the same word, in that
    // order: خَلَقۡنَٰكُمۡ is خلق + نا + كم. One peel leaves خلقنا, which is not a headword
    // and is not going to become one, so the object pronoun has to come off and the
    // subject marker after it. These are the markers that can stand in the middle --
    // an object pronoun is any of the ENCLITICS above, a subject marker is only one
    // of these, and nothing else may sit between them.
    static constexpr const char* SUBJECT_MARKERS[] = {
        "\xD8\xAA\xD9\x85\xD8\xA7",  // -tuma
        "\xD8\xAA\xD9\x85",          // -tum
        "\xD8\xAA\xD9\x86",          // -tunna
        "\xD9\x86\xD8\xA7",          // -na (we)
        "\xD9\x88\xD8\xA7",          // -uu (they)
        "\xD8\xAA",                  // -tu / -ta / -ti
    };
    // نون النسوة is not among them. A verb does take it in front of a pronoun, but
    // so does every noun that simply ends in a noon: سُبۡحَٰنَكَ would come apart as
    // سبح + ن + ك, and قُرۡآنَهُمۡ as قرا. The ending is rare and the nouns are not.
    // And these are the pronouns that can follow one. The ENCLITICS table holds
    // plural and dual endings too, and a plural ending is not something a pronoun
    // attaches behind -- بَنَاتُكُمۡ is بنات + كم, and peeling the ت off بنات as if it
    // were a subject marker would ask the index for بنا.
    static constexpr const char* OBJECT_PRONOUNS[] = {
        "\xD9\x87\xD9\x85\xD8\xA7",
        "\xD9\x83\xD9\x85\xD8\xA7",
        "\xD9\x87\xD9\x85",
        "\xD9\x87\xD9\x86",
        "\xD9\x83\xD9\x85",
        "\xD9\x83\xD9\x86",
        "\xD9\x87\xD8\xA7",
        "\xD9\x86\xD9\x8A",
        "\xD9\x87",
        "\xD9\x83",
        // -na is both: the subject "we" of عَلَّمۡنَا and the object "us" of عَلَّمۡتَنَا. Here
        // it is the outer one, which is the order the word writes them in -- the
        // subject pass below then finds the taa underneath it.
        "\xD9\x86\xD8\xA7",
    };

    // The bare stems left behind once a verb prefix came off. They are what the
    // unvowelled reconstruction below works from: a stem reached any other way is
    // a noun as far as this can tell, and rebuilding a perfect out of one would be
    // pure invention.
    constexpr size_t MAX_VERB_STEMS = 6;
    std::string verbStems[MAX_VERB_STEMS];
    size_t verbStemCount = 0;
    auto noteVerbStem = [&verbStems, &verbStemCount](const std::string& stem) {
      if (verbStemCount >= MAX_VERB_STEMS) return;
      for (size_t i = 0; i < verbStemCount; ++i) {
        if (verbStems[i] == stem) return;
      }
      verbStems[verbStemCount++] = stem;
    };

    // The stem itself, then the same stem with one attached pronoun or plural ending
    // removed. One suffix, not repeated: two stacked enclitics do not occur.
    auto addStemAndSuffixStrips = [&](const std::string& stem, const Provenance& provenance) {
      const bool fromVerbPrefix = provenance.fromVerb;
      arabicAdd(stem, provenance);
      if (fromVerbPrefix) noteVerbStem(stem);
      for (const char* enclitic : ENCLITICS) {
        const size_t n = strlen(enclitic);
        if (!hasSuffix(stem, enclitic, n)) continue;
        std::string peeled = stem.substr(0, stem.size() - n);
        // نون النسوة never follows a long alef: قُلْنَ and رَمَيْنَ shorten the stem before it,
        // so a bare noon behind an alef is a root letter. Peeling it anyway turned
        // وَكَانَ into وكا + ن -- and وكا is an index key -- where the word is و + كان.
        if (n == 2 && std::strcmp(enclitic, "\xD9\x86") == 0 && peeled.size() >= 2 && peeled.ends_with("\xD8\xA7") &&
            !nunTawkid) {
          continue;
        }
        // Every form below is this stem minus this one enclitic, respelled or not,
        // so they all carry the same suffix width: the display cuts the affix text
        // out of the base, which the respellings never touched.
        const uint8_t suffixBytes = static_cast<uint8_t>(n);
        arabicAdd(peeled, provenance, suffixBytes);
        if (fromVerbPrefix) noteVerbStem(peeled);

        // A noun ending in taa marbuta writes it as a plain taa the moment anything
        // follows it: dhurriya + -hu is dhurriyatuhu, and peeling just the pronoun
        // leaves "dhurriyat", which is not how the noun is ever written alone. Hand
        // the taa marbuta back, or the headword is never asked for.
        if (!subjectTaa && peeled.size() >= 2 && static_cast<unsigned char>(peeled[peeled.size() - 2]) == 0xD8 &&
            static_cast<unsigned char>(peeled[peeled.size() - 1]) == 0xAA) {
          std::string restored = peeled;
          restored[restored.size() - 1] = static_cast<char>(0xA9);
          arabicAdd(std::move(restored), provenance, suffixBytes);
        }
        // A final hamza sits on a yaa the moment anything follows it: أَسْمَاء + هُمْ is
        // أَسْمَائِهِمْ, and peeling only the pronoun leaves أسمائ, a seat with nothing to
        // seat. Hand the hamza its own letter back -- the broken plurals this
        // happens to (أسماء, شركاء, علماء, أنبياء) are most of the plurals in a mushaf.
        if (peeled.size() >= 2 + MIN_ARABIC_STEM_BYTES && peeled.ends_with("\xD8\xA7\xD8\xA6")) {
          std::string restored = peeled.substr(0, peeled.size() - 2) + "\xD8\xA1";
          arabicAdd(std::move(restored), provenance, suffixBytes);
        }
        // A noun whose singular ends in hamza writes it as a waw in front of the
        // sound feminine plural: سَمَاء pluralises to سَمَاوات and صَحرَاء to صَحرَاوات, so
        // peeling only the ending leaves سماو, which is not a word. Hand the hamza
        // back -- same suffix width, since the ending is untouched by the respelling.
        if (peeled.size() >= 2 + MIN_ARABIC_STEM_BYTES && peeled.ends_with("\xD8\xA7\xD9\x88")) {
          std::string restored = peeled.substr(0, peeled.size() - 2) + "\xD8\xA1";
          arabicAdd(std::move(restored), provenance, suffixBytes);
        }
        // The same shape at the other end of the word: a plural verb's "-uu" is
        // written waw + alef, but the alef drops the moment a pronoun follows, so
        // iʿtaqabuu + -haa is iʿtaqabuuhaa and peeling only the pronoun leaves a
        // bare waw standing where the ending was.
        if (peeled.size() >= 2 + MIN_ARABIC_STEM_BYTES &&
            static_cast<unsigned char>(peeled[peeled.size() - 2]) == 0xD9 &&
            static_cast<unsigned char>(peeled[peeled.size() - 1]) == 0x88) {
          peeled.resize(peeled.size() - 2);
          arabicAdd(std::move(peeled), provenance, suffixBytes);
        }
        // And last, the subject marker underneath, when what just came off was an
        // object pronoun: خَلَقۡنَٰكُمۡ is خلق + نا + كم, and one peel leaves خلقنا, which is
        // not a headword and is not going to become one. Behind every respelling
        // above, because the shape it competes with is a feminine noun carrying a
        // pronoun -- كَلِمَتُهُ, ذُرِّيَّتِهِ -- and where the word does not say otherwise the
        // noun is the commoner reading. Off the plain peel only: stacking a strip on
        // a respelling is a guess about a guess.
        bool object = false;
        for (const char* pronoun : OBJECT_PRONOUNS) object = object || std::strcmp(enclitic, pronoun) == 0;
        if (!object) continue;
        // `peeled` was moved from by the rule above; this is the same string.
        const std::string trunk = stem.substr(0, stem.size() - n);
        for (const char* marker : SUBJECT_MARKERS) {
          const size_t m = strlen(marker);
          if (!hasSuffix(trunk, marker, m)) continue;
          // The one marker a noun's own ending can be mistaken for, and the sukun
          // that settles which it is. Without that evidence this reading is not
          // offered at all -- the taa marbuta restore above already reached the noun.
          if (std::strcmp(marker, "\xD8\xAA") == 0 && !subjectTaa) continue;
          Provenance stacked = provenance;
          stacked.subjectBytes = static_cast<uint8_t>(m);
          stacked.fromVerb = true;
          arabicAdd(trunk.substr(0, trunk.size() - m), stacked, suffixBytes);
          break;
        }
      }
    };

    // Proclitics stack -- "wa-" + "bil-" + stem is ordinary Arabic -- so this strips
    // repeatedly instead of once, and offers every intermediate as a candidate: the
    // dictionary may hold the partly-clitic form even when it lacks the bare stem.
    // Each intermediate gets the suffix pass too, not just the fully stripped one at
    // the end. The loop cannot tell a proclitic from a root letter that looks like
    // one, so it keeps eating: "walafzhuhu" (and its wording) loses its waw to reach
    // "lafzhuhu", then loses the lam of the root LFZH itself to reach a dead
    // "fzhuhu". The headword wanted -- "lafzh", that first intermediate minus only
    // its pronoun -- exists in the dictionary, and suffixing only the final stem
    // would never ask for it.
    // Returns the fully stripped stem and how many bytes of proclitic came off it,
    // so the verb-prefix pass below can say where in the word its own stem starts.
    auto stripProclitics = [&](std::string base, const Base baseKind) {
      size_t prefixBytes = 0;
      for (bool changed = true; changed;) {
        changed = false;
        for (const char* proclitic : ARABIC_PROCLITICS) {
          const size_t n = strlen(proclitic);
          if (!hasPrefix(base, proclitic, n)) continue;
          base = base.substr(n);
          prefixBytes += n;
          addStemAndSuffixStrips(base, Provenance{baseKind, static_cast<uint8_t>(prefixBytes), 0, false});
          changed = true;
          break;
        }
      }
      return std::pair<std::string, size_t>{base, prefixBytes};
    };

    // Suffixes first, on the word as it stands. A proclitic strip is a guess and it
    // is destructive: "kitabuhu" (his book) starts with the kaf that "ka-" claims, so
    // everything derived after that strip is built on a mangled stem and the form
    // actually wanted -- the same word minus only its pronoun -- would never be
    // generated at all.
    auto derive = [&](const std::string& stemFolded, const Base baseKind) {
      addStemAndSuffixStrips(stemFolded, Provenance{baseKind, 0, 0, false});
      // suffixes each strip it makes
      const auto [stem, prefixBytes] = stripProclitics(stemFolded, baseKind);
      // A conjugated verb carries both ends at once: ya-...-un is one word, and the
      // headword is what is left after both come off. So the suffix pass runs again
      // over each verb-prefix-stripped stem rather than only over the bare one.
      for (const char* verbPrefix : VERB_PREFIXES) {
        const size_t n = strlen(verbPrefix);
        if (hasPrefix(stem, verbPrefix, n)) {
          addStemAndSuffixStrips(
              stem.substr(n), Provenance{baseKind, static_cast<uint8_t>(prefixBytes), static_cast<uint8_t>(n), true});
        }
      }
    };
    // A hollow verb (فعل أجوف) writes its middle radical as an alef in the perfect
    // -- قَالَ, كَانَ, بَاعَ -- and drops it the moment a subject ending starting
    // with a consonant attaches: قُلْنَا, كُنتُم, بِعْتُ. What the ending leaves in
    // front of it is two letters, one under the floor every strip below observes, so
    // nothing else in this file can reach the verb -- and a .syn table built by
    // concatenating endings onto keys answers قلنا with the geminate قلّ, whose own
    // perfect is قَلَلْنَا.
    size_t hollowStart = 0;
    size_t hollowEnding = 0;
    const std::string hollow = restoreHollowAlef(folded, &hollowStart, &hollowEnding);
    auto addHollowPerfect = [&](const bool ahead) {
      if (hollow.empty()) return;
      const std::string stem = hollow.substr(hollowStart, hollow.size() - hollowStart - hollowEnding);
      if (ahead) {
        // The vowels settle it, so this outranks even the forms reconstructed before
        // the strips ran: no guess about the word is better than the reading its own
        // vowels spell out.
        forms.erase(std::remove_if(forms.begin(), forms.end(), [&stem](const Candidate& c) { return c.form == stem; }),
                    forms.end());
        forms.insert(forms.begin(), Candidate{stem, Base::HollowAlef, static_cast<uint8_t>(hollowStart), 0,
                                              static_cast<uint8_t>(hollowEnding), 0, true});
      } else {
        arabicAdd(stem, Provenance{Base::HollowAlef, static_cast<uint8_t>(hollowStart), 0, true},
                  static_cast<uint8_t>(hollowEnding));
      }
      // Lane keys a hollow verb under either spelling -- قال and قول are both
      // articles -- and which of the two carries the verb differs from root to root,
      // so the letters the alef stands for are offered behind it.
      for (const char* radical : {"\xD9\x88", "\xD9\x8A"}) {
        std::string root = stem;
        root.replace(2, 2, radical);
        if (root != word) add(std::move(root), built);
      }
    };
    // Ahead of the strips only when the vowels prove it. وَقُلْنَا and وَعَدْنَا are
    // the same shape and the same length, and the strip that treats the waw as a root
    // letter reaches a real headword for both, so on unvowelled text this reading has
    // to wait its turn rather than outrank one derived from the word as written.
    if (vowelledAsContractedHollow(word, hollowStart)) addHollowPerfect(true);

    // The particle the word is addressed with, ahead of everything: it is the one
    // reading the spelling actually proves, and the readings it beats are not near
    // misses -- they are a proper name conjugated as a verb.
    if (startsWithVocativeYaa(word)) {
      // Both spellings of the name behind it, in the order the rest of this file
      // uses: written out first, because a superscript alef is a letter the text
      // actually put there -- إِبۡرَٰهِيمُ is only ابراهيم if it is written out -- and the
      // folded one behind it, because a name can carry a superscript of its own
      // and spelling THAT out gives موسىا, which nobody has written.
      std::vector<Candidate> called;
      const std::string spelled = baseText(word, Base::DaggerSpelled);
      if (spelled.size() > 4 && spelled.compare(0, 4, "\xD9\x8A\xD8\xA7") == 0 &&
          spelled.size() >= 4 + MIN_ARABIC_STEM_BYTES) {
        called.push_back(Candidate{spelled.substr(4), Base::DaggerSpelled, 4, 0, 0, 0, false});
      }
      if (folded.size() >= 2 + MIN_ARABIC_STEM_BYTES) {
        called.push_back(Candidate{folded.substr(2), Base::Folded, 2, 0, 0, 0, false});
      }
      for (Candidate& candidate : called) {
        candidate.vocative = true;
        forms.erase(std::remove_if(forms.begin(), forms.end(),
                                   [&candidate](const Candidate& c) { return c.form == candidate.form; }),
                    forms.end());
      }
      forms.insert(forms.begin(), called.begin(), called.end());
    }

    // The word before is the whole of the evidence for this one. لم governs a
    // مضارع مجزوم, so a hollow verb behind it is written contracted, and the
    // reading that puts the alef back leads -- ahead even of the word as folded,
    // because that spelling IS a headword here and would otherwise answer first.
    size_t jazmLed = 0;
    if (before == Neighbour::Jazm) {
      size_t jussivePrefix = 0;
      const std::string jussive = restoreHollowJussive(folded, &jussivePrefix);
      std::vector<Candidate> led;
      if (!jussive.empty()) {
        const std::string stem = jussive.substr(jussivePrefix);
        std::vector<Candidate> hollow{
            Candidate{stem, Base::HollowJussive, 0, static_cast<uint8_t>(jussivePrefix), 0, 0, true}};
        // Lane keys a hollow verb under either spelling -- قال and قول are both
        // articles -- and which of the two carries the verb differs from root to
        // root, so the letters the alef stands for are offered behind it.
        for (const char* radical : {"\xD9\x88", "\xD9\x8A"}) {
          std::string root = stem;
          root.replace(2, 2, radical);
          if (root != word) hollow.push_back(Candidate{root, Base::Reconstructed, 0, 0, 0, 0, true});
        }
        // The other verb the same three letters spell: an assimilated one (مثال),
        // which drops the waw it begins with in the imperfect whether or not
        // anything put it in the jussive -- تَجِدۡ is وجد, يَضَعۡ is وضع. Its stem is
        // the two letters with the waw put back in FRONT of them rather than
        // between them, so it is built here and not by restoreHollowJussive().
        const char vowel = contractedVowel(word);
        std::vector<Candidate> assimilated;
        if (vowel != 'u') {
          assimilated.push_back(
              Candidate{std::string("\xD9\x88") + folded.substr(jussivePrefix), Base::Reconstructed, 0, 0, 0, 0, true});
        }
        const std::string letters = folded.substr(jussivePrefix);
        if (vowel == 'i' || (vowel == 'a' && gutturalStem(letters))) {
          led = std::move(assimilated);
          led.insert(led.end(), hollow.begin(), hollow.end());
        } else {
          led = std::move(hollow);
          led.insert(led.end(), assimilated.begin(), assimilated.end());
        }
        for (const Candidate& lead : led) {
          forms.erase(
              std::remove_if(forms.begin(), forms.end(), [&lead](const Candidate& c) { return c.form == lead.form; }),
              forms.end());
        }
        forms.insert(forms.begin(), led.begin(), led.end());
        jazmLed = led.size();
      }
    }

    // A superscript alef is a letter the text actually wrote, so a reading that
    // keeps it outranks one that throws it away -- سَمَٰوَٰتِ is سماوات, and folding the
    // mark away leaves سموت, which belongs to the unrelated root سمت. Both whole
    // words go in before either is cut, and the written-out one leads: where the
    // index holds both, it is the more specific of the two -- صَلۡصَٰلٖ is صلصال, the
    // word, and صلصل, the root it is formed from, and the word is what was asked.
    //
    // Nothing is lost where it is only the mark: the demonstratives and اللّٰه are
    // keyed without the letter and هاذا, ذالك, اللاه are not keys at all, so the
    // spelled form misses and the folded one answers on the very next probe. What
    // must NOT move ahead of the folded word is the SPELLING's strips -- اللاه minus
    // its article is لاه, a real and quite wrong headword -- and they do not.
    // Both whole words went in above; these are the strips, and they follow the same
    // order. The one exception is the closed list -- وَإِلَٰهُ is و + اله, and cutting the
    // written-out spelling first reaches الاه, which the index also holds.
    const std::string spelled = spellDaggerAlifAsAlef(word);
    const std::string spelledBase = spelled == word ? std::string() : normalizeArabicHamza(stripArabicMarks(spelled));
    const bool foldedLeads = filedWithoutItsAlef(folded);
    if (!spelledBase.empty() && !foldedLeads) derive(spelledBase, Base::DaggerSpelled);
    derive(folded, Base::Folded);
    if (!spelledBase.empty() && foldedLeads) derive(spelledBase, Base::DaggerSpelled);

    // A second full pass, over the word with accusative tanween's alif carrier cut
    // off, and only after the first: the mark's alif might mask a proclitic or an
    // enclitic of its own ("wa-kitaban", and a book), and appending here can only add
    // candidates, never take one away -- unlike running the derivation on this form
    // alone, which would silently lose every word this detection is wrong about (an
    // OCR-mangled hamza-final noun, not a case marker, looks identical once the mark
    // is gone). Checked on the untouched word, before stripArabicMarks(), because that
    // is the only place the fathatan -- the signal this is tanween at all, not a root
    // letter -- still survives.
    const std::string detanweened = stripArabicTanweenAlif(word);
    if (detanweened != word) derive(normalizeArabicHamza(stripArabicMarks(detanweened)), Base::Detanweened);

    // The dual's noon drops in idafa, so what is left against the noun in front of an
    // attached pronoun is a bare yaa: rijl + -ayn + -ha is rijlayha, and peeling only
    // the pronoun stops at "rijlay", one letter short of the headword.
    //
    // Only in front of a pronoun, and only after every strip that works on the
    // word as written. A trailing yaa with nothing
    // after it is usually the word's own -- a nisba (al-Bukhari is a name, not
    // bukhar, steam; al-Dhahabi is not dhahab, gold) -- and stripping those turns an
    // honest miss into a confident wrong answer on every scholar's name in a chain of
    // transmission. Earlier in the order it is worse still: "al-bayt" suffix-strips
    // to "al-bay", whose yaa this would then eat down to a dead "al-b", while the
    // "al-" it has not shed yet still leads to bayt.
    static constexpr const char* PRONOUN_ENCLITICS[] = {
        "\xD9\x87\xD9\x85\xD8\xA7",  // -huma
        "\xD9\x83\xD9\x85\xD8\xA7",  // -kuma
        "\xD9\x87\xD9\x85",          // -hum
        "\xD9\x87\xD9\x86",          // -hunna
        "\xD9\x83\xD9\x85",          // -kum
        "\xD9\x83\xD9\x86",          // -kunna
        "\xD9\x87\xD8\xA7",          // -ha
        "\xD9\x86\xD8\xA7",          // -na
        "\xD9\x87",                  // -hu
        "\xD9\x83",                  // -ka
    };
    for (const char* pronoun : PRONOUN_ENCLITICS) {
      const size_t n = strlen(pronoun);
      // one letter past the floor: the yaa still has to come off after the pronoun
      if (folded.size() < n + MIN_ARABIC_STEM_BYTES + 2 || !folded.ends_with(pronoun)) continue;
      std::string stem = folded.substr(0, folded.size() - n);
      if (stem.size() >= 2 && static_cast<unsigned char>(stem[stem.size() - 2]) == 0xD9 &&
          static_cast<unsigned char>(stem[stem.size() - 1]) == 0x8A) {
        stem.resize(stem.size() - 2);
        // The pronoun and the dual's yaa in front of it come off together, so the
        // display shows one affix spanning both rather than a yaa on its own.
        arabicAdd(std::move(stem), Provenance{Base::Folded, 0, 0, false}, static_cast<uint8_t>(n + 2));
      }
      break;
    }

    // T4: the same reconstructions as T2, for a word that carried no marks to
    // prove them. They go here, after every strip the word itself supports, so an
    // unvowelled "yufsidu" still answers with the root it has always answered
    // with and only reaches "afsada" if that missed -- while "yanTaliqu" and
    // "yajtamiʿu", which today reach nothing at all, now have somewhere to land.
    constexpr size_t MAX_ARABIC_GUESSES = 8;
    const size_t guessCeiling = forms.size() + MAX_ARABIC_GUESSES;
    // Every reading below is built with nothing behind it -- a letter swapped for
    // the one a typesetter might have meant, an ending dropped that was never
    // written. They are told apart from the measure reconstructions so the screen
    // that offers them can say which is which.
    const Provenance guessed{Base::Guessed, 0, 0, false};
    auto guessAdd = [&add, &forms, &word, &guessed, guessCeiling](std::string value) {
      if (forms.size() >= guessCeiling || value == word) return;
      add(std::move(value), guessed);
    };
    if (!geminateVerbStem.empty()) noteVerbStem(geminateVerbStem);
    for (size_t i = 0; i < verbStemCount; ++i) {
      std::string guesses[morph::MAX_STEM_GUESSES];
      const size_t count = morph::stemGuesses(verbStems[i], guesses, morph::MAX_STEM_GUESSES);
      for (size_t j = 0; j < count; ++j) guessAdd(guesses[j]);
    }

    // The other spelling of every form, and only after all of them: a maqsura swap is
    // a guess about the typesetter, not about the word, so it must not outrank a form
    // derived from the word as written. "al-bayt" suffix-strips to "al-bay", whose twin
    // is a real key and the wrong answer, while the "al-" it has not shed yet still
    // leads to the right one.
    // Only the last letter: a medial yaa is never written as a maqsura, so
    // swapping one produced a form no dictionary can hold -- two dead SD reads on
    // every lookup of a word with a yaa in the middle, which in Arabic is most
    // verbs in the imperfect.
    std::vector<std::string> natural;
    natural.reserve(forms.size());
    for (const Candidate& candidate : forms) natural.push_back(candidate.form);
    std::string swappedFolded = morph::swapFinalMaqsura(folded);
    if (swappedFolded != folded) guessAdd(std::move(swappedFolded));
    for (const std::string& form : natural) {
      std::string swapped = morph::swapFinalMaqsura(form);
      if (swapped != form) guessAdd(std::move(swapped));
    }

    // A defective verb's final alef is written either way and the dictionaries
    // disagree: daʿaa is keyed under the maqsura spelling here, so the alef one
    // never matches. Same tier as the swap above, and for the same reason -- this
    // is a guess about the typesetter rather than about the word.
    //
    // The second guess is the same alef read as an accusative ending whose tanween
    // the text never printed: "wuhuushan" is written wuhuusha + alef, and only the
    // mark that is missing would have proved it. Both start from the whole word
    // before the strips, since a proclitic the word still carries is part of the
    // headword as often as not.
    // The two ways a final alef can be the wrong spelling, tried on the whole
    // word before the stripped forms -- the same order the chain uses everywhere
    // else, and here it is what keeps the two rules from answering for each
    // other's words.
    const auto endsInAlef = [](const std::string& form) {
      if (form.size() < MIN_ARABIC_STEM_BYTES || static_cast<unsigned char>(form[form.size() - 2]) != 0xD8 ||
          static_cast<unsigned char>(form[form.size() - 1]) != 0xA7) {
        return false;
      }
      // Unless the alef is the last letter of an ending that owns it: the -haa
      // and -naa pronouns and the plural verb's -uu are already handled as the
      // suffixes they are, and respelling or dropping their alef only asks for
      // words that cannot exist.
      static constexpr const char* ALEF_FINAL_ENDINGS[] = {
          "\xD9\x87\xD8\xA7",  // -ha
          "\xD9\x86\xD8\xA7",  // -na
          "\xD9\x88\xD8\xA7",  // -u (verb plural)
          "\xD9\x85\xD8\xA7",  // -ma (the dual half of -huma/-kuma)
      };
      for (const char* ending : ALEF_FINAL_ENDINGS) {
        if (form.size() >= 4 && form.ends_with(ending)) return false;
      }
      return true;
    };
    const auto addAlefGuesses = [&guessAdd, &endsInAlef](const std::string& form) {
      if (!endsInAlef(form)) return;
      // A defective verb's final alef is written either way and dictionaries
      // disagree: daʿaa is keyed under the maqsura spelling here, so the alef one
      // never matches. Respelling before shortening, since it keeps every letter
      // the word actually has.
      std::string maqsura = form;
      maqsura[maqsura.size() - 2] = static_cast<char>(0xD9);
      maqsura[maqsura.size() - 1] = static_cast<char>(0x89);
      guessAdd(std::move(maqsura));
      // Or the alef is an accusative ending whose tanween the text never printed:
      // "wuhuushan" is written wuhuusha + alef, and the mark that would have
      // proved it is the one the typesetter left out. Only for a word long enough
      // that what is left is still more than a bare root -- the ending rides on
      // plurals and verbal nouns, and on a four-letter word this rule is just a
      // root guess competing with the real ones.
      if (form.size() >= MIN_ARABIC_STEM_BYTES + 4) guessAdd(form.substr(0, form.size() - 2));
    };
    addAlefGuesses(folded);
    for (const std::string& form : natural) addAlefGuesses(form);

    // The same reading, for a word whose vowels did not prove it: last, so it can
    // only answer where everything derived off the word as written has missed.
    if (!vowelledAsContractedHollow(word, hollowStart)) addHollowPerfect(false);

    // Two readings the word itself picks between. Both promote a strip that is
    // already in the list rather than adding anything: order IS the answer here,
    // since lookup() takes the first candidate that hits an entry.
    if (startsWithWaslAlef(word)) {
      // فعل الأمر: the alef came off as a verb prefix, so the imperative's stem is
      // exactly the candidates that carry one.
      std::stable_partition(forms.begin(), forms.end(), [](const Candidate& c) { return c.verbPrefixBytes > 0; });
      for (Candidate& candidate : forms) {
        if (candidate.verbPrefixBytes == 0) continue;
        candidate.wasl = true;
        candidate.imperative = true;
      }
    } else if (hamzaFollowsProclitic(word)) {
      std::stable_partition(forms.begin(), forms.end(), [](const Candidate& c) { return c.prefixBytes > 0; });
    }

    // And a third, which runs after either of those rather than instead of one:
    // لَأَمۡلَأَنَّ carries a hamza behind its lam AND a doubled nun, and the nun is the
    // more specific evidence -- it names the last letter, not just the first.
    if (nunTawkid) {
      for (Candidate& candidate : forms) {
        if (candidate.suffixBytes != 2) continue;
        const std::string base = baseText(word, candidate.base);
        const size_t affixBytes = static_cast<size_t>(candidate.prefixBytes) + candidate.verbPrefixBytes + 2;
        if (base.size() > affixBytes && base.ends_with("\xD9\x86")) candidate.tawkid = true;
      }
      std::stable_partition(forms.begin(), forms.end(), [](const Candidate& c) { return c.tawkid; });
    }

    // And the one the word itself could not have told us. Contracted or not, what
    // follows لم is a verb, so every reading that cut an imperfect's subject marker
    // off goes ahead of the ones that read those same letters as part of a root.
    // Past the contracted reading, which the same particle already put in front.
    if (before == Neighbour::Jazm) {
      std::stable_partition(forms.begin() + static_cast<std::ptrdiff_t>(jazmLed), forms.end(),
                            [](const Candidate& c) { return c.verbPrefixBytes > 0; });
    }

    // A command whose hamza is its own letter rather than a wasl: nothing was cut
    // off the front, so this changes no candidate and no order -- only the name of
    // the stem, and of the pronoun hanging off it, which is an object and not a
    // possessor.
    if (isMeasureFourImperative(word)) {
      for (Candidate& candidate : forms) {
        if (candidate.prefixBytes > 0 || candidate.verbPrefixBytes > 0) continue;
        candidate.imperative = true;
        candidate.fromVerb = true;
      }
    }

    // Single-letter prefixes and suffixes are also ordinary root letters, so a good
    // number of these forms are wrong. That is affordable HERE and nowhere else: each
    // is one index probe that misses before the next is tried, and lookup() returns on
    // the first hit. getSuggestionAnchor() deliberately does not reuse any of this --
    // see it for what an over-strip costs once a fuzzy scan is anchored on it.
    return forms;
  }

  auto endsWith = [&lower](const char* suffix) {
    const size_t len = strlen(suffix);
    return lower.size() >= len && lower.compare(lower.size() - len, len, suffix) == 0;
  };

  if (lower.size() > 4 && endsWith("es")) add(lower.substr(0, lower.size() - 2));
  if (lower.size() > 3 && endsWith("s")) add(lower.substr(0, lower.size() - 1));

  if (spanish) {
    struct SpanishInfinitivePronoun {
      const char* suffix;
      size_t pronounLen;
    };

    static constexpr SpanishInfinitivePronoun PRONOUNS[] = {
        {"arme", 2},  {"arte", 2}, {"arse", 2}, {"arnos", 3}, {"aros", 2}, {"erme", 2},  {"erte", 2}, {"erse", 2},
        {"ernos", 3}, {"eros", 2}, {"irme", 2}, {"irte", 2},  {"irse", 2}, {"irnos", 3}, {"iros", 2},
    };

    for (const auto& form : PRONOUNS) {
      if (endsWith(form.suffix) && lower.size() > strlen(form.suffix) + 2) {
        add(lower.substr(0, lower.size() - form.pronounLen));
      }
    }

    auto addGerundFallback = [&](const char* suffix, const char* infinitiveEnding) {
      const size_t suffixLen = strlen(suffix);
      if (lower.size() > suffixLen + 2 && lower.compare(lower.size() - suffixLen, suffixLen, suffix) == 0) {
        add(lower.substr(0, lower.size() - suffixLen) + infinitiveEnding);
      }
    };

    addGerundFallback("andose", "ar");
    addGerundFallback("\xC3\xA1ndose", "ar");
    addGerundFallback("iendose", "er");
    addGerundFallback("iendose", "ir");
    addGerundFallback("i\xC3\xA9ndose", "er");
    addGerundFallback("i\xC3\xA9ndose", "ir");
  }

  // Deliberately NOT gated on isEnglishLanguage(). That gate matches only a
  // languageId (the folder name) of exactly "english"/"en", or an .ifo lang=
  // starting with "en" -- but most StarDict .ifo files carry no lang field at
  // all, and real folder names look like "eng", "en-ar", "FreeDict", "wiktionary".
  // So on the majority of installed dictionaries these suffix rules silently
  // never ran and "walked"/"stories" simply failed. Running them unconditionally
  // is safe: they are byte compares against ASCII suffixes, so they cannot match
  // Arabic/Persian text (all bytes >= 0x80), and Arabic returns earlier anyway.
  // The only cost is a few extra findIndexHit() calls on the miss path, which
  // already pays for findSuggestions() -- orders of magnitude more work.
  if (lower.size() > 5 && endsWith("ing")) {
    add(lower.substr(0, lower.size() - 3));
    add(lower.substr(0, lower.size() - 3) + "e");
  }
  if (lower.size() > 4 && endsWith("ed")) {
    add(lower.substr(0, lower.size() - 2));
    add(lower.substr(0, lower.size() - 1));
  }
  if (lower.size() > 5 && endsWith("ies")) add(lower.substr(0, lower.size() - 3) + "y");

  // Undo a doubled final consonant before -ed/-ing: stopped -> stop, running ->
  // run, planned -> plan, sitting -> sit. Without this the plain suffix strip
  // above yields "stopp"/"runn", which no dictionary lists. Common enough in
  // prose that its absence was a routine lookup failure.
  const auto addUndoubled = [&](const char* suffix) {
    const size_t suffixLen = strlen(suffix);
    if (lower.size() < suffixLen + 3) return;
    if (lower.compare(lower.size() - suffixLen, suffixLen, suffix) != 0) return;
    const size_t stemLen = lower.size() - suffixLen;
    const char last = lower[stemLen - 1];
    // Only for doubled ASCII consonants; "ee"/"oo" are not this pattern.
    if (last != lower[stemLen - 2]) return;
    if (last < 'a' || last > 'z') return;
    if (last == 'a' || last == 'e' || last == 'i' || last == 'o' || last == 'u') return;
    add(lower.substr(0, stemLen - 1));
  };
  addUndoubled("ed");
  addUndoubled("ing");

  // Possessives: "dog's" -> "dog". cleanWord() only trims non-alphanumerics at
  // the EDGES, so an interior apostrophe survives and the generic -s strip above
  // produces "dog'", which never matches. Handles both ASCII ' and U+2019, the
  // curly apostrophe most EPUBs actually use.
  const auto addPossessive = [&](const char* apostrophe) {
    const std::string suffix = std::string(apostrophe) + "s";
    if (lower.size() <= suffix.size()) return;
    if (lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) != 0) return;
    add(lower.substr(0, lower.size() - suffix.size()));
  };
  addPossessive("'");
  addPossessive("\xE2\x80\x99");

  return forms;
}

}  // namespace ArabicStem
