// Candidate generation for dictionary lookup.
//
// The expectations are the FULL ordered candidate list, not a prefix: the order
// is the contract (the caller probes in order and stops at the first hit), and a
// silent extra probe costs an SD read per lookup on a device with no spare I/O.

#include <ArabicStem.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

struct FormsCase {
  const char* word;
  std::vector<const char*> expected;
};

std::string join(const std::vector<std::string>& items) {
  std::string out;
  for (const std::string& item : items) {
    if (!out.empty()) out += ", ";
    out += item;
  }
  return out;
}

void expectForms(const FormsCase& testCase, const bool spanish = false) {
  const std::vector<std::string> actual = ArabicStem::fallbackForms(testCase.word, spanish);
  std::vector<std::string> expected(testCase.expected.begin(), testCase.expected.end());
  EXPECT_EQ(actual, expected) << "word: " << testCase.word << "\n  actual:   " << join(actual)
                              << "\n  expected: " << join(expected);
}

TEST(ArabicStemFallbackForms, ArabicNounsAndProclitics) {
  const FormsCase cases[] = {
      // A pronoun comes off, then the kaf that "ka-" claims is eaten too.
      {"كتابه", {"كتاب", "تابه", "تاب", "ابه"}},
      // "wal-" is taken whole rather than as "wa-" + "al-".
      {"والكتاب", {"كتاب", "تاب"}},
      {"بكتابهم", {"بكتاب", "كتابهم", "كتاب", "تابهم", "تاب", "ابهم"}},
      {"قالوا", {"قال"}},
      // Only the LAST letter is swapped between yaa and alef maqsura: a medial
      // yaa is never written the other way, so swapping one produced probes no
      // dictionary could answer.
      {"المسلمين", {"المسلم", "المسلمي", "مسلمين", "مسلم", "مسلمي", "المسلمى", "مسلمى"}},
      // The taa marbuta a pronoun suffix hides is handed back: dhurriyatuhu -> dhurriya.
      {"ذريته", {"ذريت", "ذرية"}},
      // The dual's yaa, which the attached pronoun hides, comes off last.
      // The dual's yaa comes off after the pronoun, and before the spelling
      // guesses rather than after them.
      {"رجليها", {"رجلي", "رجل", "رجلى"}},
      // Accusative tanween's alif carrier gets its own derivation pass.
      // The fathatan says the alef is a case ending, so the word without it is
      // asked for before the strips get to offer "taba" and "aba" -- either of
      // which is a real key, and the wrong answer.
      {"كتاباً", {"كتابا", "كتاب", "تابا", "ابا", "تاب", "كتابى", "تابى", "ابى"}},
  };
  for (const FormsCase& testCase : cases) expectForms(testCase);
}

TEST(ArabicStemFallbackForms, GeminateKeepsItsShadda) {
  // A doubled measure is a different word from its root, so the shadda-bearing
  // key is offered before the bare skeleton it shares with that root.
  expectForms({"عَلَّمَ", {"علّم", "علم"}});
  expectForms({"يُعَلِّمُ", {"يعلّم", "علّم", "يعلم", "علم", "اعلم", "اعلّم"}});
}

// A word whose marks identify its measure: the headword it was formed from is
// asked for before the root the strips alone would reach.
TEST(ArabicStemFallbackForms, MarksIdentifyTheMeasure) {
  expectForms({"يُفْسِدُ", {"يفسد", "افسد", "فسد"}});                         // IV
  expectForms({"يُفْسَدُ", {"يفسد", "فسد", "افسد"}});                         // passive: root first
  expectForms({"يُفَسِدُ", {"يفسد", "فسّد", "فسد", "افسد"}});                  // II with the shadda left off
  expectForms({"يَنْطَلِقُ", {"ينطلق", "انطلق", "طلق", "نطلق"}});              // VII
  expectForms({"يَجْتَمِعُ", {"يجتمع", "اجتمع", "جمع", "جتمع"}});              // VIII
  expectForms({"يَسْتَخْلِفُ", {"يستخلف", "استخلف", "خلف", "ستخلف", "اخلف"}});  // X
  expectForms({"يُؤْمِنُ", {"يؤمن", "امن", "يؤم", "ؤمن"}});                   // IV of a hamza-initial root
  expectForms({"يُقَاتِلُ", {"يقاتل", "قاتل"}});                              // III
  // Measure IV of a hollow root keeps the weak letter (ajwafa, not aajafa), so
  // that spelling is asked for too -- last, after the resolved one.
  expectForms({"يَقُولُ", {"يقول", "قال", "قول", "اقول"}});  // hollow I
  expectForms({"يَكْتُبُونَ", {"يكتبون", "كتب", "يكتب", "يكتبو", "كتبون", "كتبو", "اكتب"}});
  // The perfect's own subject marker, with no prefix to read.
  expectForms({"رَأَيْتُ", {"رأيت", "رايت", "راى", "راي"}});
  // A proclitic carries its own vowel, and comes off before the measure is read.
  expectForms({"وَيُفْسِدُ", {"ويفسد", "افسد", "فسد", "يفسد"}});
}

// Without marks there is no evidence, so the root keeps answering first and the
// derived perfect is offered only if it missed.
TEST(ArabicStemFallbackForms, UnvowelledKeepsTheRootFirst) {
  expectForms({"يفسد", {"فسد", "افسد"}});
  expectForms({"ينسخ", {"نسخ", "انسخ"}});
  expectForms({"يستخلف", {"ستخلف", "خلف", "استخلف", "اخلف"}});
  expectForms({"ينطلق", {"نطلق", "انطلق", "طلق"}});
  expectForms({"يجتمع", {"جتمع", "اجتمع", "جمع"}});
  // A noon first radical with a taa after it can only be measure VIII, so no
  // measure VII root is offered.
  expectForms({"ينتظر", {"نتظر", "انتظر", "نظر"}});
  // A stem ending in a subject ending is not a measure: no "kabuu" from
  // "katabuu", no reconstruction from a stem the endings still cling to.
  expectForms({"تنزيل", {"نزيل"}});
  expectForms({"تعليم", {"عليم"}});
}

TEST(ArabicStemFallbackForms, SpacedSelectionSplitsIntoTokens) {
  // Only a typed query can carry a space; the particle goes last because a
  // content word is the one more likely to be meant.
  expectForms({"و علّم", {"علّم", "علم"}});
}

TEST(ArabicStemFallbackForms, LatinSuffixes) {
  expectForms({"walked", {"walk", "walke"}});
  expectForms({"stories", {"stori", "storie", "story"}});
  expectForms({"stopped", {"stopp", "stoppe", "stop"}});
  expectForms({"dog's", {"dog'", "dog"}});
}

TEST(ArabicStemFallbackForms, SpanishFormsOnlyWhenEnabled) {
  // No Latin rule matches a word ending in -e, so the reflexive infinitive is
  // reachable only through the Spanish pronoun table.
  EXPECT_EQ(ArabicStem::fallbackForms("levantarse", false), std::vector<std::string>{});
  EXPECT_EQ(ArabicStem::fallbackForms("levantarse", true), std::vector<std::string>{"levantar"});
}

// A final alef is two other things often enough to be worth asking about, but
// only when it is the word's own letter rather than the tail of an ending.
TEST(ArabicStemFallbackForms, FinalAlefIsRespelledThenDropped) {
  // A defective verb keyed under the maqsura spelling, behind the proclitic it
  // carries: respelling beats shortening, which would offer an unrelated root.
  expectForms({"فدعا", {"دعا", "فدعى", "دعى"}});
  // An accusative plural whose tanween the text never printed. Shortening only
  // applies where enough of the word is left to be more than a bare root.
  expectForms({"وحوشا", {"حوشا", "وحوشى", "وحوش", "حوشى"}});
  // The alef of -haa and of the plural -uu belongs to the ending, and both are
  // already peeled as suffixes, so neither rule fires on them.
  expectForms({"قالوا", {"قال"}});
  expectForms({"رجليها", {"رجلي", "رجل", "رجلى"}});
}

// The plural verb's "-uu" is written waw + alef, but the alef drops the moment a
// pronoun follows it, so peeling only the pronoun leaves a bare waw behind.
TEST(ArabicStemFallbackForms, PluralWawThatAPronounHides) {
  const std::vector<std::string> forms = ArabicStem::fallbackForms("واعتقبوها", false);
  const auto peeled = std::find(forms.begin(), forms.end(), "اعتقب");
  ASSERT_NE(peeled, forms.end()) << join(forms);
  // Behind the forms that keep more of the word, ahead of the spelling guesses.
  EXPECT_LT(peeled - forms.begin(), 6) << join(forms);
}

// A vowelled mushaf spells its marks differently from ordinary Arabic, and a
// book that quotes scripture puts those spellings on the reading page. Every
// case here missed entirely before the marks were recognised.
TEST(ArabicStemFallbackForms, QuranicOrthography) {
  const auto resolves = [](const char* word, const char* expected) {
    const std::vector<std::string> forms = ArabicStem::fallbackForms(ArabicStem::cleanWord(word), false);
    EXPECT_NE(std::find(forms.begin(), forms.end(), expected), forms.end()) << word << " -> " << join(forms);
  };
  // The sukun is U+06E1, not U+0652, so nothing was stripped at all.
  resolves("يُفۡسِدُ", "افسد");
  resolves("وَيَسۡفِكُ", "سفك");
  // Adam is written hamza + alef where ordinary text writes the madda alef.
  resolves("ءَادَمَ", "ادم");
  // Two letters and a taa marbuta: the root's weak third letter fell off the noun.
  resolves("ءَايَةٌ", "ايى");
  resolves("لُغَةٌ", "لغو");
  resolves("صِلَةَ", "وصل");
  // The interrogative particle is a bare hamza on the front of the verb.
  resolves("أَتَجۡعَلُ", "جعل");
  // A superscript alef is the long vowel the letter would otherwise spell.
  resolves("لِلۡمَلَـٰٓئِكَةِ", "ملائكة");
  // Tanween written on the letter before its alef, rather than on the alef.
  resolves("رَغَدًا", "رغد");
  // The ornate parentheses an ayah is set in belong to the page, not the word.
  EXPECT_EQ(ArabicStem::cleanWord("﴿وَإِذۡ﴾"), "وَإِذۡ");
}

// The demonstratives are keyed without the letter their superscript alef stands
// for, so dropping the mark has to stay the first reading.
TEST(ArabicStemFallbackForms, SuperscriptAlefIsOfferedBothWays) {
  const std::vector<std::string> forms = ArabicStem::fallbackForms("هَٰذَا", false);
  const auto dropped = std::find(forms.begin(), forms.end(), "هذا");
  const auto spelled = std::find(forms.begin(), forms.end(), "هاذا");
  ASSERT_NE(dropped, forms.end()) << join(forms);
  if (spelled != forms.end()) EXPECT_LT(dropped - forms.begin(), spelled - forms.begin()) << join(forms);
}

TEST(ArabicStemCleanWord, TrimsPunctuationButKeepsDiacritics) {
  EXPECT_EQ(ArabicStem::cleanWord("word,"), "word");
  EXPECT_EQ(ArabicStem::cleanWord("«word»"), "word");
  EXPECT_EQ(ArabicStem::cleanWord("\xE2\x80\x9Cword\xE2\x80\x9D"), "word");
  // Arabic comma and question mark, which are not the ASCII ones they resemble.
  EXPECT_EQ(ArabicStem::cleanWord("كتاب،"), "كتاب");
  EXPECT_EQ(ArabicStem::cleanWord("كتاب؟"), "كتاب");
  // Soft hyphen is a layout hint even in the middle of the word.
  EXPECT_EQ(ArabicStem::cleanWord("wo\xC2\xADrd"), "word");
  EXPECT_EQ(ArabicStem::cleanWord("Word"), "word");
  // Diacritics survive: the measure detector needs them.
  EXPECT_EQ(ArabicStem::cleanWord("يُفْسِدُ"), "يُفْسِدُ");
}

TEST(ArabicStemSuggestionAnchor, StripsOnlyUnambiguousProclitics) {
  // Multi-letter proclitics cannot be root letters, so they come off.
  EXPECT_EQ(ArabicStem::suggestionAnchor("والكتاب"), "كتاب");
  // A single-letter one is also an ordinary first root letter: a wrong strip
  // here misanchors the whole fuzzy scan, so it stays.
  EXPECT_EQ(ArabicStem::suggestionAnchor("كتاب"), "كتاب");
  EXPECT_EQ(ArabicStem::suggestionAnchor("يُفْسِدُ"), "يفسد");
}

}  // namespace
