#pragma once

#include <vector>

#include "KidsReadCurriculum.h"
#include "activities/Activity.h"
#include "fontIds.h"

// "Belajar Baca" -- a parent-led learn-to-read app for small children (3-4),
// covering Indonesian letters, hijaiyah with harakat, and English letter names.
//
// PREVIEW BUILD. The screens, fonts and flow are real; progress is not stored
// and no scheduler picks what to show, so every stage starts from the top.
//
// Three levels, deliberately: track -> stage -> screen. A session is one stage,
// chosen by the parent, and a stage does exactly one thing. The alternative --
// a single deck that walks letters, then syllables, then words -- gave the
// parent no way to say "today we are just meeting the letters".
//
// The device has no speaker, so the PARENT is the voice: every card carries a
// cue line saying what to read out, and the child only presses buttons. That is
// also what the evidence asks for -- letter-sound apps show an effect with an
// adult alongside and close to none without one.
class KidsReadActivity final : public Activity {
 public:
  explicit KidsReadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("KidsRead", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  const char* activityDebugName() const override { return "KidsRead"; }

 private:
  enum class Screen : uint8_t {
    TrackList,  // pick a language
    StageList,  // pick what this session is for
    Grid,       // Grid stage: the whole set, paged
    Card,       // one big glyph -- browsed from the grid, or dealt by Practice
  };

  // Where a Card came from, which is the only thing that differs between the
  // two ways of reaching it: browsing moves through the set in order and never
  // ends, practice deals a shuffled hand and finishes.
  enum class CardSource : uint8_t { Browsing, Practice };

  const kidsread::Track& track() const { return kidsread::TRACKS[trackIndex]; }
  const kidsread::Stage& stage() const {
    const kidsread::Stage& s = track().stages[stageIndex];
    return childIndex >= 0 ? s.children[childIndex] : s;
  }

  // Starts track().stages[index] on its first screen.
  void openStage(int index);
  // Back out of a stage: to the stage list, or the track list if there is none.
  void leaveStage();

  // Grid geometry, derived from the panel, the font and the set rather than
  // fixed: the X3 is wider than the X4, and a set of "Aa Bb" pairs needs wider
  // cells than a set of single letters.
  int gridCellWidth() const;
  int gridCellHeight() const;
  int gridColumns() const;
  int gridRows() const;
  int gridPageCount() const;
  // Whether the grid draws the selected item's cue under it. Not for a picture
  // set ("Aa" -> "ayam"): there the cue names the object on the card's picture,
  // and under a grid of bare letters it reads as the letter's name. Not for an
  // empty cue (the English letter grid) either.
  bool gridShowsCue() const { return !stage().imageDir && *stage().items[gridIndex].cue; }

  // Builds `order` for a Practice stage: every index in the set, shuffled,
  // then truncated to a session's worth. Kept as an explicit list rather than
  // drawing at random each time so an item cannot appear twice in one session
  // while another never shows.
  void startPracticeSession();

  // Picks the one display cut this stage will draw every item with, and stores
  // it in bigFontId. Called when a stage starts, not per card.
  void beginStage();

  // The tallest ink a glyph of `fontId` can occupy: ascender plus descender of
  // the font the renderer will actually use, which for an Arabic track is the
  // mapped Arabic cut. NOT getLineHeight() -- advanceY is a uint8_t, so the
  // 96pt cut reports a 255px line box around 324px of ink.
  int glyphInkHeight(int fontId) const;

  // The largest display cut that fits EVERY item in the current stage. Chosen
  // per stage, not per card: sized per item, "ibu" came up at display size and
  // "mama" a third smaller, which reads as the letters changing rather than the
  // word. drawText also centres, so an overflowing word loses head and tail,
  // not just its right edge.
  int fontForSet(int maxWidth, int maxHeight) const;

  // Draws one glyph with its INK centred on (centerX, centerY) -- the line box
  // is not the ink at these sizes.
  int cueFontId() const { return stage().bigCue ? KIDSLATIN_32_FONT_ID : UI_12_FONT_ID; }

  void drawGlyph(const char* text, int centerX, int centerY, int fontId) const;

  void renderTrackList(int pageWidth, int contentTop, int contentBottom);
  void renderStageList(int pageWidth, int contentTop, int contentBottom);
  void renderGrid(int pageWidth, int contentTop, int contentBottom);
  void renderCard(int pageWidth, int contentTop, int contentBottom);

  Screen screen = Screen::TrackList;
  uint8_t trackIndex = 0;
  uint8_t stageIndex = 0;
  int childIndex = -1;  // grid cell whose child stage is open, or -1

  int listIndex = 0;  // selection in TrackList / StageList
  int gridIndex = 0;  // selection within the whole set, not within the page
  int cardIndex = 0;  // index into the set (browsing) or into `order` (practice)
  CardSource cardSource = CardSource::Browsing;
  int bigFontId = 0;  // the whole stage's display cut; set by beginStage()

  // Practice session state.
  std::vector<uint8_t> order;
};
