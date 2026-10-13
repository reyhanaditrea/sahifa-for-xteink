#pragma once

#include <cstdint>

// Content for the Belajar Baca app (KidsReadActivity): what a child is shown,
// in what order, and what the parent says about it.
//
// PREVIEW BUILD -- the tables are the real shape, but nothing yet records which
// items a child knows, so every stage starts from the top each time.
namespace kidsread {

// One thing on a card or in a grid cell.
struct Item {
  // What the child sees, huge. UTF-8; Arabic carries its harakat and is shaped
  // and placed by the renderer (see GfxRenderer::drawArabicText).
  const char* glyph;
  // What the parent says it is.
  const char* name;
  // The full line under the card: the sound, plus an example word or the one
  // visual detail that separates this letter from its look-alike.
  const char* cue;
};

// How a stage presents its set. Each set appears as a Grid stage first and a
// Practice stage after: meeting the letters and recalling them are different
// jobs, and a parent should be able to pick which one this session is.
enum class Mode : uint8_t {
  Grid,      // paged grid of the whole set; Confirm opens one card, browsable
  Practice,  // the set in random order, one card at a time
  Cards,     // the set in order, one card at a time, browsable; no grid first
};

struct Stage {
  const char* title;
  const char* subtitle;  // nullptr for none; all stages of a track or none
  Mode mode;
  const Item* items;
  uint8_t count;
  // Grid stages only: force this many columns, or 0 to fit as many as the panel
  // and the widest item allow. Set where the shape of the page teaches
  // something -- the harakat set reads as one letter per row only at three.
  uint8_t columns;
  // Folder under /kidsread/ on the SD card holding one picture per item, named
  // after Item::name ("ayam.bmp"). nullptr for stages without pictures.
  // On the card rather than in the firmware: the pictures would otherwise eat
  // the app slot's headroom, and a missing file just means no picture.
  const char* imageDir = nullptr;
  // The cue is the pictured word for the CHILD to read ("ayam"), so it gets
  // the kids' face at size. Otherwise it is the parent's note, small. The kids
  // face is letters only: a cue with spaces or punctuation must stay false.
  bool bigCue = false;
  // Grid stages only: one Cards stage per item, in item order. Confirm on a
  // cell opens that item's stage instead of its card (a letter -> its words).
  const Stage* children = nullptr;
};

struct Track {
  const char* title;
  // Drives font choice and base direction for this track's glyphs. Explicit
  // rather than sniffed per string so a Latin transliteration inside an Arabic
  // track can never flip the layout.
  bool arabic;
  const Stage* stages;
  uint8_t stageCount;
};

extern const Track TRACKS[];
extern const uint8_t TRACK_COUNT;

}  // namespace kidsread
