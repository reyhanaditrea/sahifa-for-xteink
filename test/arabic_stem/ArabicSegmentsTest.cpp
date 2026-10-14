// The decomposition the reader is shown: which affixes came off the word to
// reach the candidate the dictionary answered on.
//
// Two invariants matter more than any single label. A segmentation must
// concatenate back to something the word actually contains, and the candidate
// list itself must not change shape just because it now carries provenance --
// the order is the lookup contract, and ArabicStemTest.cpp pins it.

#include <ArabicSegments.h>
#include <ArabicStem.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct Split {
  std::string join;                                           // "و + عرض + هم"
  std::vector<std::pair<std::string, std::string>> labelled;  // {text, label}
};

// The segmentation of the first candidate whose form equals `form`.
Split splitOf(const std::string& word, const std::string& form,
              const ArabicStem::Neighbour before = ArabicStem::Neighbour::None) {
  for (const ArabicStem::Candidate& candidate : ArabicStem::fallbackCandidates(word, false, before)) {
    if (candidate.form != form) continue;
    ArabicStem::Segment segments[ArabicStem::MAX_SEGMENTS];
    const size_t count = ArabicStem::segments(word, candidate, segments, ArabicStem::MAX_SEGMENTS);
    Split split{ArabicStem::joinSegments(segments, count), {}};
    for (size_t i = 0; i < count; ++i) split.labelled.push_back({segments[i].text, segments[i].label});
    return split;
  }
  return Split{"(no candidate " + form + ")", {}};
}

TEST(ArabicSegments, AttachedPronounIsShownAsItsOwnPart) {
  const Split split = splitOf("عرضهم", "عرض");
  EXPECT_EQ(split.join, "عرض + هم");
  ASSERT_EQ(split.labelled.size(), 2u);
  EXPECT_EQ(split.labelled[0].second, "الاسم");
  EXPECT_EQ(split.labelled[1].second, "ضمير مضاف إليه");
}

TEST(ArabicSegments, StackedProcliticsSplitIntoTheParticlesAsWritten) {
  // The stripper eats "وال" in one bite; the reader sees two particles.
  const Split split = splitOf("والكتاب", "كتاب");
  EXPECT_EQ(split.join, "و + ال + كتاب");
  ASSERT_EQ(split.labelled.size(), 3u);
  EXPECT_EQ(split.labelled[0].second, "واو العطف");
  EXPECT_EQ(split.labelled[1].second, "أل التعريف");
  EXPECT_EQ(split.labelled[2].second, "الاسم");
}

TEST(ArabicSegments, PrefixAndSuffixOnTheSameWord) {
  const Split split = splitOf("بكتابهم", "كتاب");
  EXPECT_EQ(split.join, "ب + كتاب + هم");
}

TEST(ArabicSegments, ImperfectPrefixMakesTheStemAVerbAndItsPronounAnObject) {
  const Split split = splitOf("يعرضهم", "عرض");
  EXPECT_EQ(split.join, "ي + عرض + هم");
  ASSERT_EQ(split.labelled.size(), 3u);
  EXPECT_EQ(split.labelled[0].second, "حرف المضارعة");
  EXPECT_EQ(split.labelled[1].second, "الفعل");
  EXPECT_EQ(split.labelled[2].second, "ضمير المفعول");
}

TEST(ArabicSegments, PluralEndingOnAVerb) {
  const Split split = splitOf("قالوا", "قال");
  EXPECT_EQ(split.join, "قال + وا");
  ASSERT_EQ(split.labelled.size(), 2u);
  // Nothing was stripped off the front to prove it is a verb; the ending does.
  EXPECT_EQ(split.labelled[0].second, "الفعل");
  EXPECT_EQ(split.labelled[1].second, "واو الجماعة");
}

TEST(ArabicSegments, RestoredTaaMarbutaIsShownAsTheHeadwordNotTheSlice) {
  // dhurriyatuhu peels to "ذريت", which is not how the noun is ever written; the
  // stem shown is the form actually asked for.
  const Split split = splitOf("ذريته", "ذرية");
  EXPECT_EQ(split.join, "ذرية + ه");
}

TEST(ArabicSegments, ReconstructedFormsHaveNothingToSplit) {
  // يُفْسِدُ reaches افسد by rebuilding the perfect, not by cutting anything off.
  const Split split = splitOf("يُفْسِدُ", "افسد");
  EXPECT_EQ(split.join, "افسد");
  ASSERT_EQ(split.labelled.size(), 1u);
  EXPECT_EQ(split.labelled[0].second, "صيغة مشتقة");
}

TEST(ArabicSegments, HollowPerfectPutsItsAlefBackBeforeTheSubjectEnding) {
  // قُلْنَا is قَالَ + نا, not the geminate قلّ the two letters look like: the alef
  // is the middle radical, and it is only missing because the ending made it drop.
  const Split split = splitOf("قلنا", "قال");
  EXPECT_EQ(split.join, "قال + نا");
  ASSERT_EQ(split.labelled.size(), 2u);
  EXPECT_EQ(split.labelled[0].second, "الفعل");
  EXPECT_EQ(split.labelled[1].second, "ضمير الفاعل");
}

TEST(ArabicSegments, HollowPerfectWhoseLastRadicalMergedWithTheEnding) {
  // كَانَ + نا is written كُنَّا: the root's noon and the ending's are one letter
  // on the page, so only a single letter stands in front of the ending.
  const Split split = splitOf("كنا", "كان");
  EXPECT_EQ(split.join, "كان + نا");
}

TEST(ArabicSegments, CitationGluedToTheWordIsNotPartOfIt) {
  // A mushaf closes the ayah and opens the reference with no space between them, so
  // the layout hands the selector one token: the word, the ornate paren, and the
  // whole citation. Only what stands before the first of those is the word.
  EXPECT_EQ(ArabicStem::cleanWord("ٱلصَّٰغِرِينَ﴾[الأعراف: ١٠-١٣]"), "ٱلصَّٰغِرِينَ");
  EXPECT_EQ(ArabicStem::cleanWord("﴿قَالَ"), "قَالَ");
  // An ordinary word keeps everything it is written with.
  EXPECT_EQ(ArabicStem::cleanWord("well-known"), "well-known");
}

TEST(ArabicSegments, ImperativeIsReadAsOneWhenTheAlefIsWasl) {
  // ٱسۡجُدُوا۟ is سَجَدَ in the imperative. Strip the marks and the letters also spell the
  // measure IV أَسْجَدَ, which the dictionary holds -- so the alef wasla, which only
  // hamzat al-wasl is written with, has to decide before the probing starts.
  const Split split = splitOf("ٱسۡجُدُوا", "سجد");
  EXPECT_EQ(split.join, "ا + سجد + وا");
  ASSERT_EQ(split.labelled.size(), 3u);
  EXPECT_EQ(split.labelled[0].second, "همزة الوصل");
  EXPECT_EQ(split.labelled[1].second, "فعل الأمر");
  EXPECT_EQ(split.labelled[2].second, "واو الجماعة");
}

TEST(ArabicSegments, WithoutTheWaslMarkTheMeasureFourReadingStillLeads) {
  // Bare "اسجدوا" proves nothing, so the reading built on the word as written keeps
  // its place: أسجد is a headword of its own and this is the only spelling it has.
  const auto forms = ArabicStem::fallbackForms("اسجدوا", false);
  ASSERT_FALSE(forms.empty());
  EXPECT_EQ(forms.front(), "اسجد");
}

TEST(ArabicSegments, WrittenOutSuperscriptAlefLeadsTheFoldedSpelling) {
  // A superscript alef stands in for a letter the word really has, so writing it
  // out reaches the article and folding it away reaches something else: صَلۡصَٰلٖ is
  // صلصال, the word, and صلصل, the root it is formed from; سَمَٰوَٰتِ is سماوات, and
  // folded it is سموت, which belongs to the unrelated root سمت.
  for (const auto& pair :
       {std::pair<const char*, const char*>{"صَلۡصَٰلٖ", "صلصال"}, {"سَمَٰوَٰتِ", "سماوات"}, {"ضَلَٰلٖ", "ضلال"}, {"غُلَٰمٞ", "غلام"}}) {
    const auto forms = ArabicStem::fallbackForms(pair.first, false);
    ASSERT_FALSE(forms.empty()) << pair.first;
    EXPECT_EQ(forms.front(), pair.second) << pair.first;
  }
}

TEST(ArabicSegments, WordsArabicFilesWithoutTheirAlefKeepTheFoldedSpelling) {
  // The other way for a closed list: إِلَٰهٍ is filed as اله, and الاه is a different
  // word the index also holds, so the written-out reading would answer and answer
  // wrongly. The demonstratives and الله are the rest of that class.
  for (const auto& pair :
       {std::pair<const char*, const char*>{"إِلَٰهٍ", "اله"}, {"وَإِلَٰهُ", "اله"}, {"هَٰذَا", "هذا"}, {"ذَٰلِكَ", "ذلك"}}) {
    const auto forms = ArabicStem::fallbackForms(pair.first, false);
    ASSERT_FALSE(forms.empty()) << pair.first;
    EXPECT_NE(std::find(forms.begin(), forms.end(), pair.second), forms.end()) << pair.first;
    // Ahead of the spelling that writes the alef out.
    const auto folded = std::find(forms.begin(), forms.end(), pair.second);
    for (auto it = forms.begin(); it != folded; ++it) {
      EXPECT_NE(*it, std::string("الاه")) << pair.first;
      EXPECT_NE(*it, std::string("هاذا")) << pair.first;
      EXPECT_NE(*it, std::string("ذالك")) << pair.first;
    }
  }
}

TEST(ArabicSegments, VocativeYaaIsNotTheImperfectsPrefix) {
  // A mushaf writes حرف النداء as a yaa with its alef above it as a superscript,
  // glued to the name by a tatweel. Fold the marks away and a bare yaa is left at
  // the front, which every table in this file reads as حرف المضارعة -- يَٰمُوسَىٰٓ came
  // out as موسى the VERB, and يَٰقَوۡمِ came out as قام, the measure rules having read
  // it as يَقُومُ. No verb prefix is ever written with a superscript alef.
  const Split adam = splitOf("يَـٰٓـَٔادَمُ", "ادم");
  EXPECT_EQ(adam.join, "يا + ادم");
  ASSERT_EQ(adam.labelled.size(), 2u);
  EXPECT_EQ(adam.labelled[0].second, "حرف النداء");
  EXPECT_EQ(adam.labelled[1].second, "الاسم");

  // Written out the particle is يا; a name carrying a superscript of its own is
  // offered folded as well, since spelling that one out gives موسىا.
  EXPECT_EQ(splitOf("يَٰقَوۡمِ", "قوم").join, "يا + قوم");
  EXPECT_EQ(splitOf("يَٰٓإِبۡرَٰهِيمُ", "ابراهيم").join, "يا + ابراهيم");
  EXPECT_EQ(splitOf("يَٰمُوسَىٰٓ", "موسى").join, "ي + موسى");

  // The reading leads: قام is a real headword and it is what the word was
  // answering with.
  const auto forms = ArabicStem::fallbackForms("يَٰقَوۡمِ", false);
  ASSERT_FALSE(forms.empty());
  EXPECT_EQ(forms.front(), "قوم");
}

TEST(ArabicSegments, AnOrdinaryImperfectKeepsItsPrefix) {
  // The evidence is the superscript alef and nothing else: a plain yaa in front of
  // a verb is still حرف المضارعة.
  const Split split = splitOf("يعرضهم", "عرض");
  EXPECT_EQ(split.join, "ي + عرض + هم");
  ASSERT_EQ(split.labelled.size(), 3u);
  EXPECT_EQ(split.labelled[0].second, "حرف المضارعة");
  EXPECT_EQ(split.labelled[1].second, "الفعل");
}

TEST(ArabicSegments, PerfectVerbCarriesItsSubjectAndItsObjectAtOnce) {
  // خَلَقۡنَٰكُمۡ is خلق + نا + كم. One peel leaves خلقنا, which is not a headword and is
  // not going to become one, so both endings have to come off -- in the order the
  // word writes them, object first.
  const Split split = splitOf("خَلَقۡنَٰكُمۡ", "خلق");
  EXPECT_EQ(split.join, "خلق + نا + كم");
  ASSERT_EQ(split.labelled.size(), 3u);
  EXPECT_EQ(split.labelled[0].second, "الفعل");
  EXPECT_EQ(split.labelled[1].second, "ضمير الفاعل");
  EXPECT_EQ(split.labelled[2].second, "ضمير المفعول");
}

TEST(ArabicSegments, TheSukunSaysWhetherATaaIsASubjectMarkerOrANounsOwn) {
  // The same letter in the same place: كَلِمَتُهُ is كلمة + ه and خَلَقۡتَنِي is خلق + ت + ني.
  // A perfect stem is quiescent before its subject marker; a taa marbuta never is.
  const Split verb = splitOf("خَلَقۡتَنِي", "خلق");
  EXPECT_EQ(verb.join, "خلق + ت + ني");
  ASSERT_EQ(verb.labelled.size(), 3u);
  EXPECT_EQ(verb.labelled[1].second, "تاء الفاعل");

  // Quiescent on the taa itself: the third-person feminine ending.
  EXPECT_EQ(splitOf("جَآءَتۡهُمۡ", "جاء").join, "جاء + ت + هم");

  // And the noun keeps its taa marbuta, which the restore reaches first.
  EXPECT_EQ(splitOf("كَلِمَتُهُ", "كلمة").join, "كلمة + ه");
  EXPECT_EQ(splitOf("ذُرِّيَّتِهِ", "ذرية").join, "ذرية + ه");
}

TEST(ArabicSegments, UnvowelledTheNounReadingKeepsTheFront) {
  // No marks, no evidence, and the noun is the commoner word: كلمته is كلمة + ه
  // until something says otherwise.
  const auto forms = ArabicStem::fallbackForms("كلمته", false);
  const auto noun = std::find(forms.begin(), forms.end(), "كلمة");
  const auto verb = std::find(forms.begin(), forms.end(), "كلم");
  ASSERT_NE(noun, forms.end());
  EXPECT_TRUE(verb == forms.end() || noun - forms.begin() < verb - forms.begin());
}

TEST(ArabicSegments, ANounEndingInANoonIsNotAVerbCarryingOne) {
  // نون النسوة is a real ending, but every noun that simply ends in a noon has the
  // same shape: سُبۡحَٰنَكَ must not come apart as سبح + ن + ك.
  const auto forms = ArabicStem::fallbackForms("سُبۡحَٰنَكَ", false);
  EXPECT_EQ(std::find(forms.begin(), forms.end(), "سبح"), forms.end());
}

TEST(ArabicSegments, MeasureFourImperativeIsNamedAsACommand) {
  // أَنظِرۡنِيٓ is the imperative of أَنۡظَرَ, and its hamza is its own letter, not the
  // wasl a measure I command takes -- nothing comes off the front. The marks are
  // the only thing that separates the command from the perfect it is keyed under:
  // أَفۡعِلۡ puts a kasra on the second radical and leaves the third quiescent.
  const Split split = splitOf("أَنظِرۡنِيٓ", "انظر");
  EXPECT_EQ(split.join, "انظر + ني");
  ASSERT_EQ(split.labelled.size(), 2u);
  EXPECT_EQ(split.labelled[0].second, "فعل الأمر");
  EXPECT_EQ(split.labelled[1].second, "ضمير المفعول");

  // Stripped of its marks the question cannot be asked -- but نون الوقاية still
  // says the stem is a verb, because a noun's possessive is a bare yaa.
  const Split bare = splitOf("أنظرني", "انظر");
  ASSERT_EQ(bare.labelled.size(), 2u);
  EXPECT_EQ(bare.labelled[0].second, "الفعل");
}

TEST(ArabicSegments, JazmParticleIsReadOffTheWordInFrontOfIt) {
  // The three riders a particle takes without changing what it governs, and the
  // words that only look like it.
  EXPECT_EQ(ArabicStem::governs("لَمۡ"), ArabicStem::Neighbour::Jazm);
  EXPECT_EQ(ArabicStem::governs("أَلَمۡ"), ArabicStem::Neighbour::Jazm);
  EXPECT_EQ(ArabicStem::governs("أَوَلَمۡ"), ArabicStem::Neighbour::Jazm);
  EXPECT_EQ(ArabicStem::governs("فَلَمۡ"), ArabicStem::Neighbour::Jazm);
  EXPECT_EQ(ArabicStem::governs(""), ArabicStem::Neighbour::None);
  EXPECT_EQ(ArabicStem::governs("لَنۡ"), ArabicStem::Neighbour::None);
  EXPECT_EQ(ArabicStem::governs("لِمَا"), ArabicStem::Neighbour::None);
  // لِمَ folds to the same two letters and governs nothing. The kasra is the whole
  // of the difference, and a vowelled text writes it.
  EXPECT_EQ(ArabicStem::governs("لِمَ"), ArabicStem::Neighbour::None);
  // A bare conjunction is not a particle with its riders eaten off it.
  EXPECT_EQ(ArabicStem::governs("وَ"), ArabicStem::Neighbour::None);
}

TEST(ArabicSegments, WordAfterAJazmParticleIsReadAsAContractedHollowVerb) {
  // أَلَمۡ أَقُلۡ: jazm drops the waw of أَقُولُ, and what is left is three letters that
  // fold to اقل -- the elative, a headword of its own, which is what answers when
  // the word is looked at alone. The particle in front is the only evidence there
  // is, so with it the verb leads.
  const Split split = splitOf("أَقُلۡ", "قال", ArabicStem::Neighbour::Jazm);
  EXPECT_EQ(split.join, "ا + قال");
  ASSERT_EQ(split.labelled.size(), 2u);
  EXPECT_EQ(split.labelled[0].second, "حرف المضارعة");
  EXPECT_EQ(split.labelled[1].second, "الفعل");

  const auto led = ArabicStem::fallbackForms("أَقُلۡ", false, ArabicStem::Neighbour::Jazm);
  ASSERT_FALSE(led.empty());
  EXPECT_EQ(led.front(), "قال");
  // Both spellings of the root follow it: which of the two Lane keys the verb
  // under differs from root to root.
  EXPECT_NE(std::find(led.begin(), led.end(), "قول"), led.end());
}

TEST(ArabicSegments, WithoutTheParticleTheSameWordStaysTheElative) {
  // The rule must not fire on the word alone: أقل is far commoner as "fewer".
  const auto forms = ArabicStem::fallbackForms("أَقُلۡ", false);
  ASSERT_FALSE(forms.empty());
  EXPECT_EQ(std::find(forms.begin(), forms.end(), "قال"), forms.end());
  EXPECT_EQ(forms.back(), "اقل");
}

TEST(ArabicSegments, EveryHollowVerbBehindTheParticleIsRestored) {
  // The middle radical is not knowable from the contraction, so the alef stands
  // for it and both letters it can be follow behind. Words whose vowel leaves no
  // doubt that the letter came out of the middle -- see the test below for the
  // ones where it does.
  for (const auto& pair :
       {std::pair<const char*, const char*>{"يَكُن", "كان"}, {"يَنَمۡ", "نام"}, {"يَزَلۡ", "زال"}, {"نَقُلۡ", "قال"}}) {
    const auto forms = ArabicStem::fallbackForms(pair.first, false, ArabicStem::Neighbour::Jazm);
    ASSERT_FALSE(forms.empty()) << pair.first;
    EXPECT_EQ(forms.front(), pair.second) << pair.first;
  }
}

TEST(ArabicSegments, TheVowelSaysWhichLetterTheContractionTookOut) {
  // Three letters behind a jazm particle are two different verbs written the same
  // way, and the vowel on the middle one is what separates them: a damma is only
  // ever a waw taken out of the middle, a kasra is the assimilated class's own
  // vowel, and a fatha is said by both -- there the guttural decides.
  struct Case {
    const char* word;
    const char* leads;
  };
  for (const Case& reading : {
           Case{"يَكُنۡ", "كان"},  // damma: hollow, and nothing else takes one
           Case{"أَقُلۡ", "قال"},  //
           Case{"تَجِدۡ", "وجد"},  // kasra: the assimilated verb's own vowel
           Case{"يَعِدۡ", "وعد"},  //
           Case{"يَبِعۡ", "وبع"},  // a hollow verb takes a kasra too, and waits behind
           Case{"يَضَعۡ", "وضع"},  // fatha with a guttural: assimilated
           Case{"يَقَعۡ", "وقع"},  //
           Case{"يَنَمۡ", "نام"},  // fatha without one: hollow
           Case{"يَزَلۡ", "زال"},  //
       }) {
    const auto forms = ArabicStem::fallbackForms(reading.word, false, ArabicStem::Neighbour::Jazm);
    ASSERT_FALSE(forms.empty()) << reading.word;
    EXPECT_EQ(forms.front(), reading.leads) << reading.word;
  }
  // وبع is not in the dictionary, so يَبِعۡ still answers باع -- one wasted probe,
  // which is what an ordering rule costs when it guesses wrong.
  const auto sold = ArabicStem::fallbackForms("يَبِعۡ", false, ArabicStem::Neighbour::Jazm);
  EXPECT_NE(std::find(sold.begin(), sold.end(), "باع"), sold.end());
  // Both readings are always in the list -- the order is the claim, not the
  // contents, so a wrong guess costs one probe rather than an answer.
  const auto both = ArabicStem::fallbackForms("تَجِدۡ", false, ArabicStem::Neighbour::Jazm);
  EXPECT_EQ(both.front(), "وجد");
  EXPECT_NE(std::find(both.begin(), both.end(), "جاد"), both.end());
}

TEST(ArabicSegments, AnUncontractedVerbBehindTheParticleOnlyGetsItsReadingsPromoted) {
  // لَمۡ يَقُولُوا is not contracted -- the ending kept the waw -- so nothing is
  // reconstructed. What the particle still settles is that the word is a verb.
  const auto forms = ArabicStem::fallbackCandidates("يَقُولُوا", false, ArabicStem::Neighbour::Jazm);
  ASSERT_FALSE(forms.empty());
  EXPECT_GT(forms.front().verbPrefixBytes, 0u);
  const auto plain = ArabicStem::fallbackForms("يَقُولُوا", false);
  const auto promoted = ArabicStem::fallbackForms("يَقُولُوا", false, ArabicStem::Neighbour::Jazm);
  // Same candidates either way: the particle reorders the list, it does not grow it.
  EXPECT_EQ(plain.size(), promoted.size());
}

TEST(ArabicSegments, DoubledNunIsTheOathsEmphasisNotADualEnding) {
  // لَأَمۡلَأَنَّ is لَ + أَمۡلَأُ + نَّ. Folded it spells لاملان, and the ان on the end is a
  // dual marker as far as the tables can tell -- one that strips to امل, a headword
  // of its own (hope), so the wrong reading answers. The shadda rules it out: no
  // dual is written with one.
  const Split split = splitOf("لَأَمۡلَأَنَّ", "ملا");
  EXPECT_EQ(split.join, "ل + ا + ملا + ن");
  ASSERT_EQ(split.labelled.size(), 4u);
  EXPECT_EQ(split.labelled[0].second, "لام التوكيد");
  EXPECT_EQ(split.labelled[1].second, "حرف المضارعة");
  EXPECT_EQ(split.labelled[2].second, "الفعل");
  EXPECT_EQ(split.labelled[3].second, "نون التوكيد");
}

TEST(ArabicSegments, TheEmphasisReadingLeadsTheDualOne) {
  // Ordering is the whole answer: both candidates are generated, and lookup takes
  // the first that hits an entry.
  const auto forms = ArabicStem::fallbackForms("لَأَمۡلَأَنَّ", false);
  const auto emphasis = std::find(forms.begin(), forms.end(), "ملا");
  const auto dual = std::find(forms.begin(), forms.end(), "امل");
  ASSERT_NE(emphasis, forms.end());
  ASSERT_NE(dual, forms.end());
  EXPECT_LT(emphasis - forms.begin(), dual - forms.begin());
}

TEST(ArabicSegments, WithoutTheShaddaTheNunBehindAnAlefStaysARootLetter) {
  // وَكَانَ is و + كان: peeling the bare nun would leave وكا, which is an index key.
  // Only the shadda lifts that guard, and this word carries none.
  const auto forms = ArabicStem::fallbackForms("وَكَانَ", false);
  EXPECT_EQ(std::find(forms.begin(), forms.end(), "وكا"), forms.end());
}

TEST(ArabicSegments, HamzaBehindAProcliticBeatsTheWordFoldingSpells) {
  // لِأٓدَمَ is لِ + آدَم, but folding the hamza away spells لادم -- the participle of
  // لَدَمَ, an entry of its own. The hamza in the text says the whole word is a mirage.
  const Split split = splitOf("لِأٓدَمَ", "ادم");
  EXPECT_EQ(split.join, "ل + ادم");
  ASSERT_EQ(split.labelled.size(), 2u);
  EXPECT_EQ(split.labelled[0].second, "حرف جر");
}

TEST(ArabicSegments, HollowPerfectWhoseLastRadicalIsAHamza) {
  // شِئْتُمَا is شَاءَ + تما. The hamza sits on a yaa only because the contraction left
  // it with a sukun; once the alef is back it stands on the line, so the stem to
  // look up is شاء and not the شائ that putting the alef back would otherwise give.
  const Split split = splitOf("شئتما", "شاء");
  EXPECT_EQ(split.join, "شاء + تما");
  ASSERT_EQ(split.labelled.size(), 2u);
  EXPECT_EQ(split.labelled[0].second, "الفعل");
  EXPECT_EQ(split.labelled[1].second, "ضمير الفاعل");
}

TEST(ArabicSegments, HollowPerfectKeepsAConjunctionInFrontOfIt) {
  const Split split = splitOf("وكنتم", "كان");
  EXPECT_EQ(split.join, "و + كان + تم");
  ASSERT_EQ(split.labelled.size(), 3u);
  EXPECT_EQ(split.labelled[0].second, "واو العطف");
  EXPECT_EQ(split.labelled[1].second, "الفعل");
}

TEST(ArabicSegments, VowelsDecideBetweenAHollowVerbAndASoundRoot) {
  // وَقُلْنَا and وَعَدْنَا are the same shape and the same length, and the strip
  // that reads the waw as a root letter reaches a real headword for both. The damma
  // is what separates them: a sound root is فَعَلْنَا there.
  const std::vector<std::string> hollow = ArabicStem::fallbackForms("وَقُلْنَا", false);
  ASSERT_FALSE(hollow.empty());
  EXPECT_EQ(hollow.front(), "قال");
  const std::vector<std::string> sound = ArabicStem::fallbackForms("وَعَدْنَا", false);
  ASSERT_FALSE(sound.empty());
  EXPECT_NE(sound.front(), "عاد");  // still offered, but behind وعد
}

TEST(ArabicSegments, QuranicWordSplitsOnItsPronoun) {
  const Split split = splitOf("عَرَضَهُمۡ", "عرض");
  EXPECT_EQ(split.join, "عرض + هم");
}

TEST(ArabicSegments, EverySegmentationConcatenatesBackToItsBase) {
  // The one invariant arabicle's own data does not enforce: what is shown must be
  // the word, not a rewrite of it.
  const char* words[] = {"عرضهم", "والكتاب", "بكتابهم", "يعرضهم", "قالوا", "قلنا",  "وكنتم",
                         "بعت",   "كنا",     "ذريته",   "رجليها", "كتاباً", "وَيُفۡسِدُ", "المسلمين"};
  for (const char* word : words) {
    for (const ArabicStem::Candidate& candidate : ArabicStem::fallbackCandidates(word, false)) {
      ArabicStem::Segment segments[ArabicStem::MAX_SEGMENTS];
      const size_t count = ArabicStem::segments(word, candidate, segments, ArabicStem::MAX_SEGMENTS);
      ASSERT_GT(count, 0u) << word << " -> " << candidate.form;
      std::string rebuilt;
      bool sawCore = false;
      for (size_t i = 0; i < count; ++i) {
        rebuilt += segments[i].text;
        if (segments[i].kind == ArabicStem::Segment::Core) {
          EXPECT_EQ(segments[i].text, candidate.form) << word;
          sawCore = true;
        }
      }
      EXPECT_TRUE(sawCore) << word << " -> " << candidate.form;
      const std::string base = ArabicStem::baseText(word, candidate.base);
      if (candidate.base != ArabicStem::Base::Reconstructed && count > 1) {
        // Affix text comes out of the base, so the rebuilt string differs from it
        // only where the stem itself was respelled.
        EXPECT_EQ(rebuilt.size(), base.size())
            << word << " -> " << candidate.form << " (" << rebuilt << " vs " << base << ")";
      }
    }
  }
}

TEST(ArabicSegments, CandidateListIsUnchangedByCarryingProvenance) {
  const char* words[] = {"كتابه",  "والكتاب", "بكتابهم", "قالوا", "المسلمين",   "ذريته",
                         "رجليها", "كتاباً",   "يُفْسِدُ",    "وَيُفۡسِدُ", "levantarse", "running"};
  for (const char* word : words) {
    const std::vector<std::string> forms = ArabicStem::fallbackForms(word, false);
    const std::vector<ArabicStem::Candidate> candidates = ArabicStem::fallbackCandidates(word, false);
    ASSERT_EQ(forms.size(), candidates.size()) << word;
    for (size_t i = 0; i < forms.size(); ++i) EXPECT_EQ(forms[i], candidates[i].form) << word << " at " << i;
  }
}

}  // namespace
