#include <DictGloss.h>
#include <gtest/gtest.h>

#include <string>

namespace {

// A Lane article as the store hands it over: senses one per bullet, authorities
// hoisted to [n], editorial insertions in brackets.
const char* const QAALA =
    "قَالَ\n"
    "\n"
    "• قَالَ, aor. يَقُولُ, inf. n. قَوْلٌ and مَقَالٌ [1] He said; he spoke.\n"
    "• [2] The saying, or speech, of a man.\n"
    "\n"
    "Notes\n"
    "[1] S, M, K\n"
    "[2] M\n";

TEST(DictGloss, TakesTheFirstSenseThatSaysWhatTheWordMeans) {
  // Not the morphology in front of it: the aorist, the verbal nouns and who
  // reported them are what the entry is made of, not what the word means.
  EXPECT_EQ(DictGloss::gloss(QAALA), "He said; he spoke.");
}

TEST(DictGloss, ASenseThatIsAConstructionNoteIsNotAMeaning) {
  const std::string article =
      "• With سمع [as transitive by itself].\n"
      "• He heard the sound.\n";
  EXPECT_EQ(DictGloss::gloss(article), "He heard the sound.");
}

TEST(DictGloss, AnOpeningInsideABracketIsAnAsideAboutTheSenseNotTheSense) {
  const std::string article = "• [He is said to have meant this] A young camel.\n";
  EXPECT_EQ(DictGloss::gloss(article), "A young camel.");
}

TEST(DictGloss, HoistedAuthoritiesComeOutOfTheMiddleOfASense) {
  const std::string article = "• He struck [3] him with a staff.\n";
  EXPECT_EQ(DictGloss::gloss(article), "He struck  him with a staff.");
}

TEST(DictGloss, AStopInsideAnAbbreviationDoesNotEndTheRendering) {
  // Lane writes "i. e." with stops in it, and a stop followed by a lower-case
  // letter ends nothing.
  const std::string article = "• He went away, i. e. departed. And then more.\n";
  EXPECT_EQ(DictGloss::gloss(article), "He went away, i. e. departed.");
}

TEST(DictGloss, ACapitalisedWordAnswersWhenNoSubjectOrArticleDoes) {
  const std::string article = "• Dates of the best kind, brought from El-Medeeneh.\n";
  EXPECT_EQ(DictGloss::gloss(article), "Dates of the best kind, brought from El-Medeeneh.");
}

TEST(DictGloss, AnArticleWithNoRenderingAtAllStillOffersItsFirstClaim) {
  // Sihah and Arramooz are Arabic throughout; there is no English opening to find,
  // and returning nothing would leave layer 1 blank in front of a real entry.
  const std::string article = "• الرَّجُلُ الشَّدِيدُ.\n";
  EXPECT_EQ(DictGloss::gloss(article), "الرَّجُلُ الشَّدِيدُ.");
}

TEST(DictGloss, AnEntryWithoutBulletsGivesItsOpeningSentence) {
  const std::string article = "house /haʊs/, /huːs/ A building for human habitation. Also a family line.";
  // The discussion after the first sentence goes. A pronunciation the entry gives
  // after its headword stays: only a run of them at the very start of the article
  // is reliably a pronunciation rather than something the sense needs.
  EXPECT_EQ(DictGloss::gloss(article), "house /haʊs/, /huːs/ A building for human habitation.");
}

TEST(DictGloss, ALeadingPronunciationRunIsNotAMeaning) {
  const std::string article = "/haʊs/, /huːs/, /hæʊs/ A building for human habitation.";
  EXPECT_EQ(DictGloss::gloss(article), "A building for human habitation.");
}

TEST(DictGloss, AGlossTooLongToShowIsCutAtAClauseRatherThanMidWord) {
  std::string sense = "• He said a thing at very great length indeed, going on and on about it";
  sense += " without pause or stop of any kind; and afterwards he said a great deal more besides";
  sense += " which is a good deal more than layer one has any room at all to show\n";
  const std::string out = DictGloss::gloss(sense);
  EXPECT_LE(out.size(), 200u);
  EXPECT_EQ(out.substr(out.size() - 4), " \xE2\x80\xA6");
  // Cut at the clause boundary, not in the middle of a word: the separator itself
  // goes with the tail that was dropped.
  EXPECT_NE(out.find("of any kind"), std::string::npos);
  EXPECT_EQ(out.find("afterwards"), std::string::npos);
}

TEST(DictGloss, ATransliteratedNameIsOneWordAndNotAnOpening) {
  // "Kisāʾī" is not a capitalised English word with a boundary after three
  // letters: the ā and the ʾ are letters too. Reading only ASCII as letters made
  // the gloss start in the middle of the name, three words into a sentence about
  // who reported the measure.
  const std::string article = "• al-Kisāʾī said the measure is فعلان.\n";
  EXPECT_EQ(DictGloss::gloss(article), "al-Kisāʾī said the measure is فعلان.");
}

TEST(DictGloss, TwoSpacesAfterAStopEndTheSenseWhateverFollows) {
  // A cross-reference set off from the sense it follows is not part of it, even
  // though "see" is lower case and a lower-case letter after a stop is otherwise
  // the sign of an abbreviation.
  const std::string article = "• The hole of rats or mice.  see ١ in that art.\n";
  EXPECT_EQ(DictGloss::gloss(article), "The hole of rats or mice.");
}

TEST(DictGloss, NothingInMeansNothingOut) {
  EXPECT_EQ(DictGloss::gloss(""), "");
  EXPECT_EQ(DictGloss::gloss("\n\n"), "");
}

TEST(DictGloss, ASenseTooShortToBeOneIsPassedOver) {
  // "He." is not a rendering; three words is the floor.
  const std::string article =
      "• He.\n"
      "• He poured out the water.\n";
  EXPECT_EQ(DictGloss::gloss(article), "He poured out the water.");
}

}  // namespace
