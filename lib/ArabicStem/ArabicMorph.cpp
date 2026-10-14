#include "ArabicMorph.h"

namespace ArabicStem {
namespace morph {
namespace {

constexpr uint16_t CP_ALIF_MADDA = 0x0622;
constexpr uint16_t CP_ALIF_HAMZA_ABOVE = 0x0623;
constexpr uint16_t CP_WAW_HAMZA = 0x0624;
constexpr uint16_t CP_ALIF_HAMZA_BELOW = 0x0625;
constexpr uint16_t CP_ALIF = 0x0627;
constexpr uint16_t CP_BAA = 0x0628;
constexpr uint16_t CP_TAA = 0x062A;
constexpr uint16_t CP_DAL = 0x062F;
constexpr uint16_t CP_THAL = 0x0630;
constexpr uint16_t CP_ZAY = 0x0632;
constexpr uint16_t CP_SEEN = 0x0633;
constexpr uint16_t CP_SAD = 0x0635;
constexpr uint16_t CP_DAD = 0x0636;
constexpr uint16_t CP_TAH = 0x0637;
constexpr uint16_t CP_ZAH = 0x0638;
constexpr uint16_t CP_FAA = 0x0641;
constexpr uint16_t CP_KAF = 0x0643;
constexpr uint16_t CP_LAM = 0x0644;
constexpr uint16_t CP_MEEM = 0x0645;
constexpr uint16_t CP_NOON = 0x0646;
constexpr uint16_t CP_HAA = 0x0647;
constexpr uint16_t CP_WAW = 0x0648;
constexpr uint16_t CP_ALIF_MAQSURA = 0x0649;
constexpr uint16_t CP_YAA = 0x064A;
constexpr uint16_t CP_ALIF_WASLA = 0x0671;

constexpr uint16_t CP_TATWEEL = 0x0640;
constexpr uint16_t CP_FATHATAN = 0x064B;
constexpr uint16_t CP_SHADDA = 0x0651;
constexpr uint16_t CP_SUKUN = 0x0652;
constexpr uint16_t CP_DAGGER_ALIF = 0x0670;
// The Quranic annotation signs. A vowelled mushaf writes its sukun as the first
// of these rather than as U+0652, so a word quoted from one carries no mark this
// would otherwise recognise -- and the sukun is half the evidence the measure
// rules read.
constexpr uint16_t CP_QURANIC_FIRST = 0x06D6;
constexpr uint16_t CP_QURANIC_SUKUN = 0x06E1;
constexpr uint16_t CP_QURANIC_LAST = 0x06ED;

// A root is three letters, so no rule may leave less than that standing.
constexpr size_t MIN_STEM_UNITS = 3;

bool isMark(const uint16_t cp) {
  return cp == CP_TATWEEL || (cp >= CP_FATHATAN && cp <= 0x065F) || cp == CP_DAGGER_ALIF ||
         (cp >= CP_QURANIC_FIRST && cp <= CP_QURANIC_LAST);
}

// Unmarked never contradicts: a writer who left the vowel off has said nothing
// about it, and a rule that demanded the mark would fire on almost no real text.
bool ok(const Unit& u, const Vowel vowel) { return u.vowel == V_NONE || u.vowel == vowel; }

// Same reading for the sukun, plus the shadda that would make it a different
// measure entirely.
bool quiescent(const Unit& u) { return (u.sukun || u.vowel == V_NONE) && !u.shadda; }

bool isWeak(const Unit& u) { return u.cp == CP_WAW || u.cp == CP_YAA || u.cp == CP_ALIF_MAQSURA; }

// The taa of measure VIII assimilates to the first radical: iSTalaha is written
// iSTalaha with Taa, but a first radical from these two sets pulls it to Tah or
// Dal instead, and the written word no longer shows a taa at all.
bool assimilatedInfix(const Unit& first, const Unit& infix) {
  if (infix.cp == CP_TAA) return true;
  if (infix.cp == CP_TAH) {
    return first.cp == CP_SAD || first.cp == CP_DAD || first.cp == CP_TAH || first.cp == CP_ZAH;
  }
  if (infix.cp == CP_DAL) return first.cp == CP_DAL || first.cp == CP_THAL || first.cp == CP_ZAY;
  return false;
}

class Emitter {
 public:
  Emitter(std::string* out, const size_t cap) : out(out), cap(cap) {}

  void one(const std::string& value) {
    if (count >= cap || value.empty()) return;
    for (size_t i = 0; i < count; ++i) {
      if (out[i] == value) return;
    }
    out[count++] = value;
  }

  // A run of units, optionally behind a letter the measure adds back.
  void units(const Unit* u, const size_t from, const size_t to, const uint16_t prefix = 0) {
    std::string value;
    if (prefix != 0) appendUnit(value, prefix);
    for (size_t i = from; i < to; ++i) appendUnit(value, u[i].cp, u[i].shadda);
    one(value);
  }

  size_t size() const { return count; }

 private:
  std::string* out;
  size_t cap;
  size_t count = 0;
};

// The perfect's own subject markers: -tu/-ta/-ti, -tum, -tunna, -tumaa, -naa,
// -na. Peeling one lands on the bare past tense, which for a sound triliteral is
// the headword itself.
size_t perfectSuffixUnits(const Unit* u, const size_t n) {
  const auto is = [&](const size_t i, const uint16_t cp) { return i < n && u[i].cp == cp; };
  if (n >= MIN_STEM_UNITS + 3 && is(n - 3, CP_TAA) && is(n - 2, CP_MEEM) && is(n - 1, CP_ALIF)) return 3;
  if (n >= MIN_STEM_UNITS + 2 && is(n - 2, CP_TAA) && (is(n - 1, CP_MEEM) || is(n - 1, CP_NOON))) return 2;
  if (n >= MIN_STEM_UNITS + 2 && is(n - 2, CP_NOON) && is(n - 1, CP_ALIF)) return 2;
  if (n >= MIN_STEM_UNITS + 1 && (is(n - 1, CP_TAA) || is(n - 1, CP_NOON))) return 1;
  return 0;
}

// The imperfect's own endings (-uuna, -iina, -aani, -uu) and the attached
// pronouns, so a rule that wants a bare 4-to-6 unit verb can get to one.
size_t trailingUnitsToPeel(const Unit* u, const size_t n) {
  const auto is = [&](const size_t i, const uint16_t cp) { return i < n && u[i].cp == cp; };
  if (n < MIN_STEM_UNITS + 2) {
    if (n >= MIN_STEM_UNITS + 1 && (is(n - 1, CP_HAA) || is(n - 1, CP_KAF))) return 1;
    return 0;
  }
  if ((is(n - 2, CP_WAW) || is(n - 2, CP_YAA) || is(n - 2, CP_ALIF)) && is(n - 1, CP_NOON)) return 2;
  if (is(n - 2, CP_WAW) && is(n - 1, CP_ALIF)) return 2;
  if (is(n - 2, CP_HAA) && (is(n - 1, CP_MEEM) || is(n - 1, CP_NOON) || is(n - 1, CP_ALIF))) return 2;
  if (is(n - 2, CP_KAF) && (is(n - 1, CP_MEEM) || is(n - 1, CP_NOON) || is(n - 1, CP_ALIF))) return 2;
  if (is(n - 2, CP_NOON) && is(n - 1, CP_ALIF)) return 2;
  if (is(n - 1, CP_HAA) || is(n - 1, CP_KAF)) return 1;
  return 0;
}

// One pass of the measure rules over a word already reduced to its verb.
// Returns what it emitted; 0 means "no rule recognised this shape".
size_t applyMeasureRules(const Unit* u, const size_t n, Emitter& emit) {
  const size_t before = emit.size();
  const Unit& prefix = u[0];
  const Unit& r1 = u[1];
  const Unit& r2 = n > 2 ? u[2] : Unit{};
  const Unit& r3 = n > 3 ? u[3] : Unit{};

  // Five and six units first: those shapes carry a measure's own letters, and
  // the four-unit rules below would read them as something shorter.
  if (n == 6 && r1.cp == CP_SEEN && r2.cp == CP_TAA && quiescent(r1)) {
    // X: yastaKHLifu -> istaKHLafa, and the root behind it.
    emit.units(u, 1, 6, CP_ALIF);
    if (u[4].cp == CP_YAA) {
      // Hollow X: yastaTIIʿu is istaTAAʿa, with the yaa written as an alif.
      std::string hollow;
      appendUnit(hollow, CP_ALIF);
      appendUnit(hollow, CP_SEEN);
      appendUnit(hollow, CP_TAA);
      appendUnit(hollow, u[3].cp, u[3].shadda);
      appendUnit(hollow, CP_ALIF);
      appendUnit(hollow, u[5].cp, u[5].shadda);
      emit.one(hollow);
    }
    emit.units(u, 3, 6);
    return emit.size() - before;
  }
  if (n == 6 && r1.cp == CP_TAA && r3.cp == CP_ALIF) {
    emit.units(u, 1, 6);  // VI: yataQAATalu -> taQAATala
    return emit.size() - before;
  }
  if (n == 5 && r1.cp == CP_TAA && ok(r2, V_A) && r3.shadda) {
    emit.units(u, 1, 5);  // V: yataʿaLLamu -> taʿaLLama
    std::string bare;
    for (size_t i = 1; i < 5; ++i) appendUnit(bare, u[i].cp);
    emit.one(bare);
    return emit.size() - before;
  }
  if (n == 5 && quiescent(r1) && !isWeak(r3)) {
    if (r1.cp == CP_NOON && r2.cp != CP_TAA) {
      emit.units(u, 1, 5, CP_ALIF);  // VII: yanTaliqu -> inTalaqa
      emit.units(u, 2, 5);           // and the root, TaLaQa
      return emit.size() - before;
    }
    if (assimilatedInfix(r1, r2)) {
      // VIII: yaJTamiʿu -> iJTamaʿa, root JMʿ. A noon first radical lands here
      // too rather than in VII above, since nt- can only be VIII.
      emit.units(u, 1, 5, CP_ALIF);
      std::string root;
      appendUnit(root, r1.cp, r1.shadda);
      appendUnit(root, u[3].cp, u[3].shadda);
      appendUnit(root, u[4].cp, u[4].shadda);
      emit.one(root);
      return emit.size() - before;
    }
  }
  if (n == 5 && prefix.vowel == V_U && r2.cp == CP_ALIF && ok(r1, V_A)) {
    emit.units(u, 1, 5);  // III: yuQAAtilu -> QAAtala
    return emit.size() - before;
  }

  if (n != 4) return 0;

  if (prefix.vowel == V_U) {
    if (r1.cp == CP_WAW_HAMZA) {
      // IV of a hamza-initial root: yuʾminu is listed under aamana, whose alef
      // is the one this puts back.
      std::string value;
      appendUnit(value, CP_ALIF);
      appendUnit(value, r2.cp, r2.shadda);
      appendUnit(value, r3.cp, r3.shadda);
      emit.one(value);
      if (r2.shadda) {
        std::string bare;
        appendUnit(bare, CP_ALIF);
        appendUnit(bare, r2.cp);
        appendUnit(bare, r3.cp);
        emit.one(bare);
      }
      return emit.size() - before;
    }
    if (r2.shadda || r1.vowel == V_A) {
      // II, whether or not the writer bothered with the shadda.
      std::string doubled;
      appendUnit(doubled, r1.cp);
      appendUnit(doubled, r2.cp, true);
      appendUnit(doubled, r3.cp, r3.shadda);
      emit.one(doubled);
      emit.units(u, 1, 4);
      return emit.size() - before;
    }
    if (quiescent(r1) && ok(r2, V_I)) {
      emit.units(u, 1, 4, CP_ALIF);  // IV: yuFSidu -> aFSada
      emit.units(u, 1, 4);           // and the root it shares with measure I
      return emit.size() - before;
    }
    if (quiescent(r1) && r2.vowel == V_A) {
      // Passive: yuFSadu is measure I's root read passively, or IV's.
      emit.units(u, 1, 4);
      emit.units(u, 1, 4, CP_ALIF);
      return emit.size() - before;
    }
    return 0;
  }

  // prefix.vowel == V_A below.
  if (r2.shadda) {
    emit.units(u, 1, 4);  // V's verbal noun, taQaDDum -> QaDDama
    std::string bare;
    for (size_t i = 1; i < 4; ++i) appendUnit(bare, u[i].cp);
    emit.one(bare);
    return emit.size() - before;
  }
  if (isWeak(r2) && r2.vowel == V_NONE && r1.vowel != V_NONE) {
    // Hollow measure I: the imperfect's waw or yaa is an alef in the perfect --
    // yaQUulu is QAAla.
    std::string hollow;
    appendUnit(hollow, r1.cp, r1.shadda);
    appendUnit(hollow, CP_ALIF);
    appendUnit(hollow, r3.cp, r3.shadda);
    emit.one(hollow);
    emit.units(u, 1, 4);
    return emit.size() - before;
  }
  emit.units(u, 1, 4);  // Sound measure I: yaKTubu -> KaTaBa
  return emit.size() - before;
}

}  // namespace

size_t scanUnits(const std::string& s, Unit* out, const size_t cap) {
  size_t count = 0;
  for (size_t i = 0; i < s.size();) {
    const unsigned char lead = static_cast<unsigned char>(s[i]);
    // Arabic is two bytes per letter in UTF-8; anything else is not a word this
    // can read, and a partial reading would be worse than none.
    if (lead < 0xD8 || lead > 0xDB || i + 1 >= s.size()) return 0;
    const unsigned char trail = static_cast<unsigned char>(s[i + 1]);
    if ((trail & 0xC0) != 0x80) return 0;
    const uint16_t cp = static_cast<uint16_t>(((lead & 0x1F) << 6) | (trail & 0x3F));
    i += 2;

    if (isMark(cp)) {
      if (count == 0) continue;  // a mark with no letter to sit on
      Unit& unit = out[count - 1];
      switch (cp) {
        case CP_FATHATAN:
          unit.vowel = V_AN;
          break;
        case 0x064C:
          unit.vowel = V_UN;
          break;
        case 0x064D:
          unit.vowel = V_IN;
          break;
        case 0x064E:
          unit.vowel = V_A;
          break;
        case 0x064F:
          unit.vowel = V_U;
          break;
        case 0x0650:
          unit.vowel = V_I;
          break;
        case CP_SHADDA:
          unit.shadda = true;
          break;
        case CP_SUKUN:
        case CP_QURANIC_SUKUN:
          unit.sukun = true;
          break;
        default:
          break;  // tatweel, dagger alef, and the rest carry no evidence
      }
      continue;
    }

    if (count >= cap) return 0;
    uint16_t letter = cp;
    // The same hamza-seat folding the rest of the chain does, so a rule never
    // has to spell a letter two ways.
    if (letter == CP_ALIF_MADDA || letter == CP_ALIF_HAMZA_ABOVE || letter == CP_ALIF_HAMZA_BELOW ||
        letter == CP_ALIF_WASLA) {
      letter = CP_ALIF;
    }
    out[count] = Unit{letter, V_NONE, false, false};
    ++count;
  }
  return count;
}

void appendUnit(std::string& out, const uint16_t cp, const bool shadda) {
  out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
  out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  if (shadda) {
    out.push_back(static_cast<char>(0xD9));
    out.push_back(static_cast<char>(0x91));
  }
}

size_t procliticUnits(const Unit* u, const size_t n) {
  const auto at = [&](const size_t i, const uint16_t cp) { return i < n && u[i].cp == cp; };
  // Longest first, so wa-al- is taken whole rather than read as wa- then al-.
  const bool leadsConjunction = at(0, CP_WAW) || at(0, CP_FAA) || at(0, CP_BAA) || at(0, CP_KAF) || at(0, CP_LAM);
  if (leadsConjunction && at(1, CP_ALIF) && at(2, CP_LAM) && n >= 3 + MIN_STEM_UNITS) {
    // The article's own lam assimilates to a sun letter, which shows up as a
    // shadda on the next letter: that is a noun, not a verb behind a prefix.
    return u[3].shadda ? 0 : 3;
  }
  if (at(0, CP_LAM) && at(1, CP_LAM) && n >= 2 + MIN_STEM_UNITS) return u[2].shadda ? 0 : 2;
  if (at(0, CP_ALIF) && at(1, CP_LAM) && n >= 2 + MIN_STEM_UNITS) return u[2].shadda ? 0 : 2;
  if (leadsConjunction && n >= 1 + MIN_STEM_UNITS) return u[1].shadda ? 0 : 1;
  return 0;
}

size_t measureCandidates(const Unit* u, const size_t n, std::string* out, const size_t cap) {
  Emitter emit(out, cap);
  if (n < MIN_STEM_UNITS + 1 || n > MAX_UNITS) return 0;

  // A past tense with its own subject marker: no prefix, so this is checked on
  // its own terms before anything below looks at u[0].
  if (const size_t suffix = perfectSuffixUnits(u, n); suffix > 0) {
    const size_t stem = n - suffix;
    const bool quiescentBefore = u[stem - 1].sukun || (suffix == 1 && u[n - 1].sukun);
    if (quiescentBefore) {
      if (stem == MIN_STEM_UNITS && u[stem - 1].cp == CP_YAA) {
        // A final yaa of the perfect is as often written with alef maqsura, and
        // dictionaries disagree: raʾaa is listed both ways.
        std::string maqsura;
        for (size_t i = 0; i + 1 < stem; ++i) appendUnit(maqsura, u[i].cp, u[i].shadda);
        appendUnit(maqsura, CP_ALIF_MAQSURA);
        emit.one(maqsura);
      }
      emit.units(u, 0, stem);
      return emit.size();
    }
  }

  // The interrogative particle is a bare hamza written onto the verb, and once
  // the seats are folded it is indistinguishable from a first-person prefix --
  // except that a real subject prefix follows it, carrying its own vowel.
  // "a-tajʿalu" is that shape, and without this the whole question misses.
  size_t start = 0;
  if (n >= 5 && u[0].cp == CP_ALIF && u[0].vowel == V_A && !u[0].shadda &&
      (u[1].cp == CP_YAA || u[1].cp == CP_TAA || u[1].cp == CP_NOON) && (u[1].vowel == V_A || u[1].vowel == V_U)) {
    start = 1;
  }
  const Unit* word = u + start;
  const size_t wordLength = n - start;

  // Everything below reads a subject prefix, and the vowel written on it is the
  // whole point: without one there is no evidence, and a guess here would
  // outrank forms derived from the word as written.
  const uint16_t first = word[0].cp;
  if (first != CP_YAA && first != CP_TAA && first != CP_NOON && first != CP_ALIF) return 0;
  if (word[0].shadda) return 0;
  if (word[0].vowel != V_A && word[0].vowel != V_U) return 0;
  for (size_t i = 0; i < wordLength; ++i) {
    if (word[i].vowel == V_AN || word[i].vowel == V_UN || word[i].vowel == V_IN) return 0;  // tanween: a noun
  }

  // The rules want the verb alone. If the word carries an ending, peel one and
  // look again -- but only a couple of times, so a long word cannot spin here.
  Unit work[MAX_UNITS];
  for (size_t i = 0; i < wordLength; ++i) work[i] = word[i];
  size_t length = wordLength;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (length >= 4 && length <= 6 && applyMeasureRules(work, length, emit) > 0) return emit.size();
    const size_t peel = trailingUnitsToPeel(work, length);
    if (peel == 0 || length - peel < 4) break;
    length -= peel;
  }
  return emit.size();
}

size_t stemGuesses(const std::string& bareStem, std::string* out, const size_t cap) {
  Unit u[MAX_UNITS];
  const size_t n = scanUnits(bareStem, u, MAX_UNITS);
  Emitter emit(out, cap);
  if (n < MIN_STEM_UNITS) return 0;

  if (u[0].cp == CP_WAW_HAMZA) {
    // The stem a verb prefix left behind still carries the root's hamza seat.
    std::string value;
    appendUnit(value, CP_ALIF);
    for (size_t i = 1; i < n; ++i) appendUnit(value, u[i].cp, u[i].shadda);
    emit.one(value);
    return emit.size();
  }

  if (n == 3) {
    if (isWeak(u[1])) {
      // Hollow: the root's waw or yaa is written as an alef in the perfect.
      std::string hollow;
      appendUnit(hollow, u[0].cp);
      appendUnit(hollow, CP_ALIF);
      appendUnit(hollow, u[2].cp);
      emit.one(hollow);
      if (u[1].cp == CP_YAA) {
        std::string fourth;
        appendUnit(fourth, CP_ALIF);
        fourth += hollow;
        emit.one(fourth);
      }
      // Measure IV of the same root keeps the weak letter rather than resolving
      // it -- jaafa is hollow, but ajwafa is not -- so both spellings are asked
      // for, the resolved one first.
      emit.units(u, 0, 3, CP_ALIF);
      return emit.size();
    }
    if (u[2].cp == CP_WAW || u[2].cp == CP_YAA) {
      // Defective: a final waw or yaa of the root is written as an alef in the
      // perfect -- daʿawa is daʿaa. The measure IV reading comes after, since an
      // unvowelled final yaa is as often the root's own letter.
      std::string defective;
      appendUnit(defective, u[0].cp);
      appendUnit(defective, u[1].cp);
      appendUnit(defective, CP_ALIF);
      emit.one(defective);
    }
    // A final alef maqsura is the defective perfect already, so what is missing
    // is only the measure's own alef: ʿashiya -> aʿshaa.
    if (u[0].cp != CP_ALIF) emit.units(u, 0, 3, CP_ALIF);  // IV
    return emit.size();
  }

  // A trailing waw or yaa on an unvowelled stem is usually a subject ending that
  // has not come off yet, and reading "katabuu" as measure VIII would offer
  // "kabuu" as its root. An alef maqsura is never an ending, so a defective verb
  // still gets its reconstruction.
  const bool trailingEnding = u[n - 1].cp == CP_WAW || u[n - 1].cp == CP_YAA;
  // A weak third letter makes the root guess below meaningless (the perfect
  // resolves it to an alef), but the measure's own alef still goes on the front,
  // so the perfect stays reachable: yastawidu -> istawada.
  const bool weakMiddle = n == 4 && isWeak(u[2]);
  if (weakMiddle && !trailingEnding && u[0].cp != CP_ALIF && assimilatedInfix(u[0], u[1])) {
    emit.units(u, 0, 4, CP_ALIF);
    return emit.size();
  }
  if (n == 4 && !weakMiddle && !trailingEnding && u[0].cp != CP_ALIF) {
    if (u[0].cp == CP_NOON && u[1].cp != CP_TAA) {
      emit.units(u, 0, 4, CP_ALIF);  // VII
      emit.units(u, 1, 4);
      return emit.size();
    }
    if (assimilatedInfix(u[0], u[1])) {
      emit.units(u, 0, 4, CP_ALIF);  // VIII
      std::string root;
      appendUnit(root, u[0].cp);
      appendUnit(root, u[2].cp);
      appendUnit(root, u[3].cp);
      emit.one(root);
      return emit.size();
    }
    return emit.size();
  }

  if (n == 5 && u[0].cp == CP_SEEN && u[1].cp == CP_TAA) {
    // X. Its root is already reachable through the stacked ya-st- prefix, so
    // only the perfect is added here.
    emit.units(u, 0, 5, CP_ALIF);
    return emit.size();
  }

  return emit.size();
}

std::string swapFinalMaqsura(const std::string& s) {
  if (s.size() < 2) return s;
  const unsigned char lead = static_cast<unsigned char>(s[s.size() - 2]);
  const unsigned char trail = static_cast<unsigned char>(s[s.size() - 1]);
  if (lead != 0xD9 || (trail != 0x89 && trail != 0x8A)) return s;
  std::string out(s);
  out[out.size() - 1] = static_cast<char>(trail ^ 0x03);
  return out;
}

}  // namespace morph
}  // namespace ArabicStem
