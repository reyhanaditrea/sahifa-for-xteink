#include "ArabicSegments.h"

#include <cstring>

namespace ArabicStem {
namespace {

struct Affix {
  const char* text;
  const char* label;
};

// Proclitics as they are WRITTEN, not as the stripper takes them: it eats "وال"
// in one bite because that sequence cannot open a root, but a reader is looking
// at two particles and wants to see two. Longest first, so "لل" is not read as
// two lams and "ال" wins over the bare lam that starts it.
constexpr Affix PROCLITICS[] = {
    {"\xD8\xA7\xD9\x84", "أل التعريف"},              // al
    {"\xD9\x84\xD9\x84", "لام الجر مع أل التعريف"},  // lil
    {"\xD9\x88", "واو العطف"},                       // wa
    {"\xD9\x81", "فاء العطف"},                       // fa
    {"\xD8\xA8", "حرف جر"},                          // bi
    {"\xD9\x83", "حرف جر"},                          // ka
    {"\xD9\x84", "حرف جر"},                          // li
};

// The enclitics ArabicStem::fallbackCandidates peels, in the same longest-first
// order, each with the name its own table gives as a comment. Two labels where
// the same letters mean different things on a verb and on a noun -- هم is the
// object of the one and the possessor of the other -- picked by Candidate::fromVerb.
struct Enclitic {
  const char* text;
  const char* nounLabel;
  const char* verbLabel;
  // Endings only a verb takes. The stem they came off is a verb no matter how it
  // was reached, so "قالوا" is قال the VERB plus واو الجماعة even though nothing
  // was stripped off its front to prove it.
  bool verbOnly = false;
};
constexpr Enclitic ENCLITIC_LABELS[] = {
    {"\xD9\x87\xD9\x85\xD8\xA7", "ضمير مضاف إليه", "ضمير المفعول"},       // -huma
    {"\xD9\x83\xD9\x85\xD8\xA7", "ضمير مضاف إليه", "ضمير المفعول"},       // -kuma
    {"\xD8\xAA\xD9\x85\xD8\xA7", "ضمير مضاف إليه", "ضمير الفاعل", true},  // -tuma
    {"\xD9\x87\xD9\x85", "ضمير مضاف إليه", "ضمير المفعول"},               // -hum
    {"\xD9\x87\xD9\x86", "ضمير مضاف إليه", "ضمير المفعول"},               // -hunna
    {"\xD9\x83\xD9\x85", "ضمير مضاف إليه", "ضمير المفعول"},               // -kum
    {"\xD9\x83\xD9\x86", "ضمير مضاف إليه", "ضمير المفعول"},               // -kunna
    {"\xD8\xAA\xD9\x85", "ضمير مضاف إليه", "ضمير الفاعل", true},          // -tum
    {"\xD8\xAA\xD9\x86", "ضمير مضاف إليه", "ضمير الفاعل", true},          // -tunna
    {"\xD9\x87\xD8\xA7", "ضمير مضاف إليه", "ضمير المفعول"},               // -ha
    {"\xD9\x86\xD8\xA7", "ضمير مضاف إليه", "ضمير الفاعل"},                // -na
    // نون الوقاية + ياء المتكلم, and a noun never takes it: the possessive is a bare
    // yaa (كِتَابِي), so the noon in front of it means the stem is a verb.
    {"\xD9\x86\xD9\x8A", "ضمير المفعول", "ضمير المفعول", true},  // -ni
    {"\xD9\x88\xD9\x86", "علامة الجمع", "علامة الرفع"},          // -un
    {"\xD9\x8A\xD9\x86", "علامة الجمع", "علامة النصب"},          // -in
    {"\xD8\xA7\xD9\x86", "علامة المثنى", "علامة المثنى"},        // -an
    {"\xD8\xA7\xD8\xAA", "جمع مؤنث سالم", "جمع مؤنث سالم"},      // -at
    {"\xD9\x88\xD8\xA7", "واو الجماعة", "واو الجماعة", true},    // -u (verb plural)
    {"\xD8\xA9", "تاء مربوطة", "تاء مربوطة"},                    // -a
    {"\xD9\x87", "ضمير مضاف إليه", "ضمير المفعول"},              // -hu
    {"\xD9\x83", "ضمير مضاف إليه", "ضمير المفعول"},              // -ka
    {"\xD9\x8A", "ياء المتكلم", "ضمير المفعول"},                 // -i
    {"\xD8\xAA", "تاء التأنيث", "تاء الفاعل"},                   // -tu / -ta / -ti
    {"\xD9\x86", "نون", "نون النسوة"},                           // -na (3fp)
};

const Enclitic* findEnclitic(const std::string& suffix) {
  for (const Enclitic& enclitic : ENCLITIC_LABELS) {
    if (suffix == enclitic.text) return &enclitic;
  }
  return nullptr;
}

constexpr const char* FUTURE_SEEN = "\xD8\xB3";  // sa-
constexpr const char* LABEL_IMPERFECT = "حرف المضارعة";
constexpr const char* LABEL_FUTURE = "حرف الاستقبال";
constexpr const char* LABEL_AUGMENT = "زيادة الصيغة";
constexpr const char* LABEL_UNKNOWN_SUFFIX = "لاحقة";
constexpr const char* LABEL_NOUN = "الاسم";
constexpr const char* LABEL_VERB = "الفعل";
constexpr const char* LABEL_DERIVED = "صيغة مشتقة";
// The alef an imperative starts with is not the imperfect's prefix but hamzat
// al-wasl, and what follows it is the command itself.
constexpr const char* LABEL_WASL = "همزة الوصل";
constexpr const char* LABEL_IMPERATIVE = "فعل الأمر";
// نون التوكيد and the oath's lam are a pair: the lam that opens لَأَمۡلَأَنَّ is not the
// preposition its letter also spells, it is what the nun at the far end answers.
constexpr const char* LABEL_TAWKID_NUN = "نون التوكيد";
constexpr const char* LABEL_TAWKID_LAM = "لام التوكيد";
// The particle a name is addressed with. What follows it is a noun by definition:
// nothing is ever called to by a verb.
constexpr const char* LABEL_NIDA = "حرف النداء";

// Every label in this file is an Arabic grammatical name, and a dictionary that is
// not Arabic reaches the same display path: the English side of an English-Indonesian
// lookup would come back labelled الاسم. A word with no Arabic letter in it has no
// Arabic grammar to report, and saying nothing is the honest answer.
bool hasArabicLetter(const std::string& text) {
  for (size_t i = 0; i + 1 < text.size(); ++i) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    if (lead >= 0xD8 && lead <= 0xDB) return true;
  }
  return false;
}

bool push(Segment* out, const size_t cap, size_t& count, std::string text, const Segment::Kind kind,
          const char* label) {
  if (count >= cap || text.empty()) return false;
  out[count++] = Segment{std::move(text), kind, label};
  return true;
}

// Splits the run of proclitic bytes the stripper took into the particles a
// reader sees. An unrecognised remainder is emitted whole rather than dropped:
// the segments must always concatenate back to the word.
void pushProclitics(const std::string& prefix, const bool tawkid, const bool vocative, Segment* out, const size_t cap,
                    size_t& count) {
  if (vocative) {
    // Whole, whether it is written يا or the bare yaa the folding leaves behind.
    push(out, cap, count, prefix, Segment::Affix, LABEL_NIDA);
    return;
  }
  if (tawkid && prefix == "\xD9\x84") {
    push(out, cap, count, prefix, Segment::Affix, LABEL_TAWKID_LAM);
    return;
  }
  size_t pos = 0;
  while (pos < prefix.size()) {
    bool matched = false;
    for (const Affix& affix : PROCLITICS) {
      const size_t n = std::strlen(affix.text);
      if (prefix.compare(pos, n, affix.text) != 0) continue;
      push(out, cap, count, prefix.substr(pos, n), Segment::Affix, affix.label);
      pos += n;
      matched = true;
      break;
    }
    if (!matched) {
      push(out, cap, count, prefix.substr(pos), Segment::Affix, LABEL_UNKNOWN_SUFFIX);
      return;
    }
  }
}

const char* encliticLabel(const std::string& suffix, const bool fromVerb) {
  // The dual's yaa comes off together with the pronoun in front of it, and other
  // rules peel a respelled ending, so not every suffix is one table entry.
  const Enclitic* enclitic = findEnclitic(suffix);
  if (enclitic == nullptr) return LABEL_UNKNOWN_SUFFIX;
  return fromVerb ? enclitic->verbLabel : enclitic->nounLabel;
}

}  // namespace

Standing standing(const std::string& word, const Candidate& candidate) {
  // A mark in the word licensed the reading: the wasl alef that only an imperative
  // is written with, the shadda under نون التوكيد, the sukun that makes a taa a
  // subject marker, the vowels that spell a contracted hollow verb.
  if (candidate.wasl || candidate.tawkid || candidate.imperative || candidate.base == Base::HollowAlef ||
      candidate.base == Base::HollowJussive) {
    return Standing::Proved;
  }
  if (candidate.base == Base::Guessed) return Standing::Guessed;
  if (candidate.base == Base::Reconstructed) return Standing::Rebuilt;

  const size_t affixBytes = static_cast<size_t>(candidate.prefixBytes) + candidate.verbPrefixBytes +
                            candidate.subjectBytes + candidate.suffixBytes;
  if (affixBytes == 0) return Standing::AsWritten;
  // A cut is what the base minus its affixes actually spells. When the form is
  // something else, a repair was made on top of the cut -- the taa marbuta handed
  // back, a hamza given its own letter, a plural waw restored -- and that is one
  // more thing to be wrong about.
  const std::string base = baseText(word, candidate.base);
  if (base.size() <= affixBytes) return Standing::Rebuilt;
  const size_t front = static_cast<size_t>(candidate.prefixBytes) + candidate.verbPrefixBytes;
  const size_t width = base.size() - affixBytes;
  return base.compare(front, width, candidate.form) == 0 ? Standing::Cut : Standing::Respelled;
}

const char* standingLabel(const Standing standing) {
  switch (standing) {
    case Standing::AsWritten:
      return "the word as written";
    case Standing::Proved:
      return "the marks prove this cut";
    case Standing::Cut:
      return "affixes cut off";
    case Standing::Respelled:
      return "cut, then respelled";
    case Standing::Rebuilt:
      return "rebuilt from the marks";
    case Standing::Guessed:
      return "spelling guess";
  }
  return "";
}

size_t segments(const std::string& word, const Candidate& candidate, Segment* out, const size_t cap) {
  if (cap == 0) return 0;
  size_t count = 0;
  if (!hasArabicLetter(word)) {
    // The form, unnamed. Nothing below this line has anything true to say about it.
    push(out, cap, count, candidate.form, Segment::Core, "");
    return count;
  }
  // A subject marker is only ever peeled off a verb, so its presence settles what
  // the stem is before any ending is looked at.
  // A vocative settles it the other way: nothing is ever called to by a verb.
  bool verb = !candidate.vocative && (candidate.fromVerb || candidate.verbPrefixBytes > 0 ||
                                      candidate.subjectBytes > 0 || candidate.imperative);
  const size_t affixBytes = static_cast<size_t>(candidate.prefixBytes) + candidate.verbPrefixBytes +
                            candidate.subjectBytes + candidate.suffixBytes;

  const std::string base = affixBytes == 0 ? std::string() : baseText(word, candidate.base);
  std::string suffix;
  if (candidate.suffixBytes > 0 && base.size() > affixBytes) {
    suffix = base.substr(base.size() - candidate.suffixBytes);
    const Enclitic* enclitic = findEnclitic(suffix);
    if (enclitic != nullptr && enclitic->verbOnly) verb = true;
  }
  if (affixBytes == 0 || base.size() <= affixBytes) {
    // Nothing was cut, or the base no longer matches the offsets (a candidate the
    // measure detector built, or a word whose respelling changed length). Either
    // way there is one thing to show: the form itself.
    const bool built = candidate.base == Base::Reconstructed || candidate.base == Base::Guessed;
    push(out, cap, count, candidate.form, Segment::Core,
         built ? LABEL_DERIVED : (candidate.imperative ? LABEL_IMPERATIVE : (verb ? LABEL_VERB : LABEL_NOUN)));
    return count;
  }

  pushProclitics(base.substr(0, candidate.prefixBytes), candidate.tawkid, candidate.vocative, out, cap, count);

  if (candidate.verbPrefixBytes > 0) {
    const std::string prefix = base.substr(candidate.prefixBytes, candidate.verbPrefixBytes);
    // "سي" is the future particle on the imperfect's own prefix, and "يست" carries
    // the measure's augment behind it; either way the first letter is the marker
    // and whatever follows belongs to the form.
    const bool future = prefix.compare(0, std::strlen(FUTURE_SEEN), FUTURE_SEEN) == 0;
    push(out, cap, count, prefix.substr(0, 2), Segment::Affix,
         candidate.wasl ? LABEL_WASL : (future ? LABEL_FUTURE : LABEL_IMPERFECT));
    if (prefix.size() > 2) {
      push(out, cap, count, prefix.substr(2), Segment::Affix, future ? LABEL_IMPERFECT : LABEL_AUGMENT);
    }
  }

  // The stem as PROBED, not as sliced out of the base: the taa marbuta and the
  // plural waw the peel restores are part of the headword being asked for, and
  // showing the slice instead would show a word that is never written.
  push(out, cap, count, candidate.form, Segment::Core,
       candidate.imperative ? LABEL_IMPERATIVE : (verb ? LABEL_VERB : LABEL_NOUN));

  // خَلَقۡنَٰكُمۡ shows three parts, not two: the verb, who did it, and to whom.
  if (candidate.subjectBytes > 0 && base.size() > affixBytes) {
    const std::string subject =
        base.substr(base.size() - candidate.suffixBytes - candidate.subjectBytes, candidate.subjectBytes);
    push(out, cap, count, subject, Segment::Affix, encliticLabel(subject, true));
  }
  if (!suffix.empty()) {
    push(out, cap, count, suffix, Segment::Affix, candidate.tawkid ? LABEL_TAWKID_NUN : encliticLabel(suffix, verb));
  }
  return count;
}

std::string joinSegments(const Segment* segments, const size_t count) {
  std::string out;
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) out += " + ";
    out += segments[i].text;
  }
  return out;
}

}  // namespace ArabicStem
