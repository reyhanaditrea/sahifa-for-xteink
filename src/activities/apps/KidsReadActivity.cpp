#include "KidsReadActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

using kidsread::Item;
using kidsread::Mode;

namespace {

// A session is short on purpose: attention at 3-4 runs out long before a
// 26-letter set does, and a stage that always ends leaves the child wanting the
// next one. The Grid stage is the unbounded one, because a parent walking the
// letters at their own pace is not a test.
constexpr int kPracticeCards = 8;

// Padding around the glyph's real ink box in a grid cell. The cell is sized
// from the font and from the widest item in the set, never from a fixed pixel
// count: 48pt Andika is 163px of ink, so the old 104px cell drew its frame
// straight through the letter.
constexpr int kGridCellPadX = 14;
constexpr int kGridCellPadY = 12;

// The grid is the one screen that shows a whole set at once, so it gets its own
// cut. A Latin cell carries both cases ("Aa"), which is about 150px wide at the
// card's 48pt -- two columns, seven pages for the alphabet. At 32pt the pair
// fits three to a row and the set lands in three pages.
constexpr int kGridFontId = KIDSLATIN_32_FONT_ID;

// The square a card's picture is drawn into. The SD files are made at this size
// (1-bit BMP), so drawBitmap never has to scale them.
constexpr int kPictureSize = 240;

}  // namespace

// ---------------------------------------------------------------- geometry

int KidsReadActivity::gridCellWidth() const {
  // The widest item decides the cell for the whole set, so every cell is the
  // same size and no letter has to shrink to fit one. "Ww" is a good deal wider
  // than "Ii", and a grid of different-sized letters teaches the wrong thing.
  const auto dir = track().arabic ? BidiUtils::BidiBaseDir::RTL : BidiUtils::BidiBaseDir::LTR;
  int widest = 0;
  for (uint8_t i = 0; i < stage().count; i++) {
    widest = std::max(widest, renderer.getTextWidth(kGridFontId, stage().items[i].glyph, EpdFontFamily::REGULAR, dir));
  }
  return widest + kGridCellPadX * 2;
}

int KidsReadActivity::gridCellHeight() const { return glyphInkHeight(kGridFontId) + kGridCellPadY * 2; }

int KidsReadActivity::gridColumns() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int usable = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int fit = std::max(2, usable / gridCellWidth());
  return stage().columns > 0 ? std::min<int>(stage().columns, fit) : fit;
}

int KidsReadActivity::gridRows() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The cue line under the grid is part of the screen, not an overlay: without
  // reserving it the last row is drawn underneath the text.
  const int cueBand = !stage().imageDir ? renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing : 0;
  const int usable = renderer.getScreenHeight() - metrics.topPadding - metrics.headerHeight -
                     metrics.verticalSpacing * 2 - metrics.buttonHintsHeight - cueBand;
  return std::max(1, usable / gridCellHeight());
}

int KidsReadActivity::gridPageCount() const {
  const int perPage = gridColumns() * gridRows();
  return (stage().count + perPage - 1) / perPage;
}

// ---------------------------------------------------------------- sessions

void KidsReadActivity::startPracticeSession() {
  order.clear();
  order.reserve(stage().count);
  for (uint8_t i = 0; i < stage().count; i++) order.push_back(i);
  // Fisher-Yates over the whole set, then take the first kPracticeCards.
  // Drawing at random per card instead would let one letter come up three
  // times while another never appears -- which a child notices and a parent
  // then has to explain.
  for (int i = static_cast<int>(order.size()) - 1; i > 0; i--) {
    const int j = static_cast<int>(random(i + 1));
    std::swap(order[i], order[j]);
  }
  if (static_cast<int>(order.size()) > kPracticeCards) order.resize(kPracticeCards);
  cardIndex = 0;
  cardSource = CardSource::Practice;
}

// ---------------------------------------------------------------- text

int KidsReadActivity::glyphInkHeight(int fontId) const {
  // Arabic is requested through the Latin ids on purpose: ArabicFontSystem maps
  // KIDSLATIN_96 -> KIDSARABIC_88 and KIDSLATIN_48 -> KIDSARABIC_48, so the
  // renderer swaps in the Arabic cut, shapes the joins and places the harakat.
  // Which means the metrics that matter are the RESOLVED font's, not the id we
  // hand to drawText -- the Arabic cut sits lower and reaches further down.
  const int resolved = track().arabic ? renderer.getResolvedArabicFontId(fontId) : fontId;
  return renderer.getFontAscenderSize(resolved) + renderer.getFontDescenderSize(resolved);
}

int KidsReadActivity::fontForSet(int maxWidth, int maxHeight) const {
  // Biggest first, stepping down only when something in the set would not fit.
  // The smallest cut is here for the long words ("sepatu", "kelapa"): a word
  // that overflows is not a smaller word, it is a word with its first and last
  // letter cut off, because drawText centres what it draws.
  // 72pt is Latin-only (no Arabic twin to map to), so Arabic skips it.
  const int ladder[] = {KIDSLATIN_96_FONT_ID, KIDSLATIN_72_FONT_ID, KIDSLATIN_48_FONT_ID, KIDSLATIN_32_FONT_ID};
  const auto dir = track().arabic ? BidiUtils::BidiBaseDir::RTL : BidiUtils::BidiBaseDir::LTR;
  for (const int fontId : ladder) {
    if (track().arabic && fontId == KIDSLATIN_72_FONT_ID) continue;
    if (glyphInkHeight(fontId) > maxHeight) continue;
    bool fits = true;
    for (uint8_t i = 0; i < stage().count && fits; i++) {
      fits = renderer.getTextWidth(fontId, stage().items[i].glyph, EpdFontFamily::REGULAR, dir) <= maxWidth;
    }
    if (fits) return fontId;
  }
  return KIDSLATIN_32_FONT_ID;
}

void KidsReadActivity::beginStage() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  // The band a big glyph gets: the card's, minus the cue line and the hints --
  // and minus the picture, for a stage that has them, so every card in the
  // stage keeps the same letter size whether or not its picture is present.
  int band = renderer.getScreenHeight() - metrics.topPadding - metrics.verticalSpacing * 2 - metrics.buttonHintsHeight -
             renderer.getLineHeight(cueFontId()) * 2;
  if (stage().imageDir) band -= kPictureSize + metrics.verticalSpacing;
  bigFontId = fontForSet(pageWidth - metrics.contentSidePadding * 2, band);
}

void KidsReadActivity::openStage(int index) {
  stageIndex = static_cast<uint8_t>(index);
  childIndex = -1;
  beginStage();
  switch (stage().mode) {
    case Mode::Grid:
      gridIndex = 0;
      screen = Screen::Grid;
      break;
    case Mode::Practice:
      startPracticeSession();
      screen = Screen::Card;
      break;
    case Mode::Cards:
      cardIndex = 0;
      cardSource = CardSource::Browsing;
      screen = Screen::Card;
      break;
  }
}

// A track of one stage has nothing to pick, so it skips the stage list both
// ways: in from the track list, and back out to it.
void KidsReadActivity::leaveStage() {
  if (track().stageCount == 1) {
    listIndex = trackIndex;
    screen = Screen::TrackList;
  } else {
    listIndex = stageIndex;
    screen = Screen::StageList;
  }
}

void KidsReadActivity::drawGlyph(const char* text, int centerX, int centerY, int fontId) const {
  const auto dir = track().arabic ? BidiUtils::BidiBaseDir::RTL : BidiUtils::BidiBaseDir::LTR;
  const int width = renderer.getTextWidth(fontId, text, EpdFontFamily::REGULAR, dir);
  // drawText takes the TOP of the line box and puts the baseline one ascender
  // below it, so centring the ink means backing off half the real ink height --
  // the line box would put a 96pt letter well above where the eye expects it.
  const int y = centerY - glyphInkHeight(fontId) / 2;
  renderer.drawText(fontId, centerX - width / 2, y, text, true, EpdFontFamily::REGULAR, dir);
}

// ---------------------------------------------------------------- lifecycle

void KidsReadActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void KidsReadActivity::loop() {
  using Button = MappedInputManager::Button;
  const bool back = mappedInput.wasReleased(Button::Back);
  const bool confirm = mappedInput.wasReleased(Button::Confirm);
  const bool left = mappedInput.wasReleased(Button::Left);
  const bool right = mappedInput.wasReleased(Button::Right);
  const bool up = mappedInput.wasReleased(Button::Up);
  const bool down = mappedInput.wasReleased(Button::Down);
  if (!(back || confirm || left || right || up || down)) return;

  switch (screen) {
    case Screen::TrackList:
      if (back) {
        finish();
        return;
      }
      if (up) listIndex = (listIndex + kidsread::TRACK_COUNT - 1) % kidsread::TRACK_COUNT;
      if (down) listIndex = (listIndex + 1) % kidsread::TRACK_COUNT;
      if (confirm) {
        trackIndex = static_cast<uint8_t>(listIndex);
        listIndex = 0;
        if (track().stageCount == 1) {
          openStage(0);
        } else {
          screen = Screen::StageList;
        }
      }
      break;

    case Screen::StageList: {
      const int count = track().stageCount;
      if (back) {
        listIndex = trackIndex;
        screen = Screen::TrackList;
        break;
      }
      if (up) listIndex = (listIndex + count - 1) % count;
      if (down) listIndex = (listIndex + 1) % count;
      if (confirm) openStage(listIndex);
      break;
    }

    case Screen::Grid: {
      const int count = stage().count;
      const int columns = gridColumns();
      if (back) {
        leaveStage();
        break;
      }
      // On an Arabic grid the next item sits to the LEFT of the current one, so
      // the buttons swap with the script -- pressing toward the next cell has
      // to move to the next letter, not away from it.
      const bool forward = track().arabic ? left : right;
      const bool backward = track().arabic ? right : left;
      if (backward) gridIndex = (gridIndex + count - 1) % count;
      if (forward) gridIndex = (gridIndex + 1) % count;
      // Vertical movement clamps instead of wrapping: a wrapped column jump
      // lands on an unrelated letter, which is disorienting on the screen
      // whose whole job is "meet them, in order".
      if (up) gridIndex = std::max(0, gridIndex - columns);
      if (down) gridIndex = std::min(count - 1, gridIndex + columns);
      if (confirm) {
        if (stage().children) {
          childIndex = gridIndex;
          beginStage();
          cardIndex = 0;
        } else {
          cardIndex = gridIndex;
        }
        cardSource = CardSource::Browsing;
        screen = Screen::Card;
      }
      break;
    }

    case Screen::Card:
      if (cardSource == CardSource::Browsing) {
        const int count = stage().count;
        if (back) {
          // Browsing returns to where it was entered from: the grid (at the
          // cell it opened), or for a Cards stage, the stage list.
          if (childIndex >= 0) {
            gridIndex = childIndex;
            childIndex = -1;
            screen = Screen::Grid;
          } else if (stage().mode == Mode::Cards) {
            leaveStage();
          } else {
            gridIndex = cardIndex;
            screen = Screen::Grid;
          }
          break;
        }
        if (left) cardIndex = (cardIndex + count - 1) % count;
        if (right || confirm) cardIndex = (cardIndex + 1) % count;
        break;
      }
      // Practice: Left is "Belum", Right is "Bisa". Both advance -- a card is
      // never re-shown inside one session, because a 3-year-old reads a repeat
      // as being corrected rather than as being helped. The last card returns
      // straight to the stage list: no score screen.
      if (back) {
        listIndex = stageIndex;
        screen = Screen::StageList;
        break;
      }
      if (left || right || confirm) {
        cardIndex++;
        if (cardIndex >= static_cast<int>(order.size())) {
          listIndex = stageIndex;
          screen = Screen::StageList;
        }
      }
      break;
  }
  requestUpdate();
}

// ---------------------------------------------------------------- render

void KidsReadActivity::renderTrackList(int pageWidth, int contentTop, int contentBottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Baca");
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentBottom - contentTop}, kidsread::TRACK_COUNT, listIndex,
               [](int index) { return std::string(kidsread::TRACKS[index].title); });
  const auto labels = mappedInput.mapLabels("Keluar", "Pilih", "", "", /*rtlSwap=*/false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Naik", "Turun");
}

void KidsReadActivity::renderStageList(int pageWidth, int contentTop, int contentBottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, track().title);
  const kidsread::Track& t = track();
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentBottom - contentTop}, t.stageCount, listIndex,
      [&t](int index) { return std::string(t.stages[index].title); },
      t.stages[0].subtitle ? [&t](int index) { return std::string(t.stages[index].subtitle); }
                           : std::function<std::string(int)>());
  const auto labels = mappedInput.mapLabels("Kembali", "Mulai", "", "", /*rtlSwap=*/false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Naik", "Turun");
}

void KidsReadActivity::renderGrid(int pageWidth, int contentTop, int contentBottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int columns = gridColumns();
  const int perPage = columns * gridRows();
  const int page = gridIndex / perPage;

  char subtitle[32];
  snprintf(subtitle, sizeof(subtitle), "hal. %d/%d", page + 1, gridPageCount());
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, stage().title,
                 gridPageCount() > 1 ? subtitle : nullptr);

  const int gridWidth = pageWidth - metrics.contentSidePadding * 2;
  const int cellWidth = gridWidth / columns;
  // Rows are spread over the band rather than stacked at their minimum height:
  // a short set would otherwise sit in the top third with the rest of the panel
  // blank, which reads as "the page is broken" rather than "this is all of it".
  const int cueBand = !stage().imageDir ? renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing : 0;
  const int rows = gridRows();
  const int band = contentBottom - contentTop - cueBand;
  const int cellHeight = std::max(gridCellHeight(), band / rows);
  const int first = page * perPage;
  const int last = std::min(first + perPage, static_cast<int>(stage().count));

  for (int i = first; i < last; i++) {
    const int slot = i - first;
    // An Arabic grid fills right-to-left, so alif lands in the top-RIGHT cell
    // and the rows run the way the child will read them. Mirroring the column
    // here rather than reversing the item order keeps `gridIndex` in curriculum
    // order, so Left/Right still step alif, ba, ta.
    const int column = track().arabic ? (columns - 1 - slot % columns) : (slot % columns);
    const int cx = metrics.contentSidePadding + column * cellWidth + cellWidth / 2;
    const int cy = contentTop + (slot / columns) * cellHeight + cellHeight / 2;
    if (i == gridIndex) {
      // A frame, not an inverted fill: a filled cell behind a big black glyph
      // leaves the letter as a white silhouette, which is not the shape the
      // child is being asked to learn.
      renderer.drawRoundedRect(cx - cellWidth / 2 + 4, cy - cellHeight / 2 + 4, cellWidth - 8, cellHeight - 8, 3, 12,
                               true);
    }
    drawGlyph(stage().items[i].glyph, cx, cy, kGridFontId);
  }

  // The selected item's cue, so the grid works as a teaching screen on its own:
  // the parent reads down the rows without opening every card.
  if (gridShowsCue()) {
    renderer.drawCenteredText(UI_12_FONT_ID,
                              contentBottom - metrics.verticalSpacing - renderer.getLineHeight(UI_12_FONT_ID),
                              stage().items[gridIndex].cue);
  }

  // rtlSwap mirrors the two hint labels for an Arabic set, matching the button
  // swap in loop().
  const auto labels = mappedInput.mapLabels("Kembali", "Buka", "Sebelum", "Lanjut", track().arabic);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "Naik", "Turun");
}

void KidsReadActivity::renderCard(int pageWidth, int contentTop, int contentBottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool practice = cardSource == CardSource::Practice;
  const int itemIndex = practice ? order[cardIndex] : cardIndex;
  const Item& item = stage().items[itemIndex];

  const int topRow = contentTop + 10;
  if (practice) {
    // Progress dots, not "3/8": a number means nothing at this age, while the
    // parent still sees how much of the session is left.
    constexpr int kDotRadius = 5;
    constexpr int kDotGap = 18;
    const int total = static_cast<int>(order.size());
    const int dotsWidth = (total - 1) * kDotGap;
    for (int i = 0; i < total; i++) {
      const int x = pageWidth / 2 - dotsWidth / 2 + i * kDotGap;
      if (i <= cardIndex) {
        renderer.fillRoundedRect(x - kDotRadius, topRow - kDotRadius, kDotRadius * 2, kDotRadius * 2, kDotRadius,
                                 Color::Black);
      } else {
        renderer.drawRoundedRect(x - kDotRadius, topRow - kDotRadius, kDotRadius * 2, kDotRadius * 2, 1, kDotRadius,
                                 true);
      }
    }
  } else {
    // Browsing has no end, so it shows a position instead of a progress bar --
    // and the parent, not the child, is the one reading it.
    char position[24];
    snprintf(position, sizeof(position), "%d / %d", cardIndex + 1, stage().count);
    renderer.drawCenteredText(UI_10_FONT_ID, topRow - renderer.getLineHeight(UI_10_FONT_ID) / 2, position);
  }

  // The glyph owns the screen: no header, no chrome, nothing else for the eye
  // to land on but the letter and the parent's one line.
  // Two spacings clear of the hints: one left the word crowding the buttons.
  const int cueY = contentBottom - metrics.verticalSpacing * 2 - renderer.getLineHeight(cueFontId());
  int glyphBottom = cueY;
  if (stage().imageDir) {
    // The picture sits just above the cue, the letter above it. The picture's
    // slot is kept even when its file is missing, so the letter and the word
    // stay put as the child pages through the set.
    glyphBottom = cueY - metrics.verticalSpacing - kPictureSize;
    char path[64];
    snprintf(path, sizeof(path), "/kidsread/%s/%s.bmp", stage().imageDir, item.name);
    HalFile file;
    if (Storage.exists(path) && Storage.openFileForRead("KIDS", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawBitmap(bitmap, (pageWidth - kPictureSize) / 2, glyphBottom, kPictureSize, kPictureSize);
      }
      file.close();
    }
  }
  drawGlyph(item.glyph, pageWidth / 2, (topRow + glyphBottom) / 2, bigFontId);
  renderer.drawCenteredText(cueFontId(), cueY, item.cue);

  const auto labels = practice ? mappedInput.mapLabels("Selesai", "", "Belum", "Bisa", /*rtlSwap=*/false)
                               : mappedInput.mapLabels("Kembali", "", "Sebelum", "Lanjut", /*rtlSwap=*/false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void KidsReadActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight;

  switch (screen) {
    case Screen::TrackList:
      renderTrackList(pageWidth, contentTop, contentBottom);
      break;
    case Screen::StageList:
      renderStageList(pageWidth, contentTop, contentBottom);
      break;
    case Screen::Grid:
      renderGrid(pageWidth, contentTop, contentBottom);
      break;
    case Screen::Card:
      // A card has no header, so its content starts where the header would be.
      renderCard(pageWidth, metrics.topPadding + metrics.verticalSpacing, contentBottom);
      break;
  }

  renderer.displayBuffer();
}
