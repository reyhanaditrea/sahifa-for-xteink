#pragma once

#include <Epub/Page.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../DictionaryStore.h"
#include "../Activity.h"

struct Rect;

// Two screens over one lookup.
//
// Layer 1 is the brief: what the word was read as, the one sentence the article
// can be reduced to, and the other readings that reached an entry of their own.
// It is what a reader wants nine times in ten, and it fits without paging.
// Layer 2 is the article itself, one press further in. Back walks the same way
// out, so the article is never a dead end the reader has to leave the dictionary
// to escape.
//
// An entry with no gloss to show -- nothing in it reads as a meaning -- has no
// layer 1 worth drawing, so the lookup opens on the article.
class DictionaryDefinitionActivity final : public Activity {
 public:
  DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                               const DictionaryLookupResult& lookup, int readerFontId, int definitionFontId,
                               int marginLeft, int marginTop, bool renderPageBackground = true)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        page(std::move(page)),
        headword(lookup.headword),
        decomposition(lookup.decomposition),
        definition(lookup.definition),
        gloss(lookup.gloss),
        readings(lookup.readings),
        truncated(lookup.truncated),
        readerFontId(readerFontId),
        definitionFontId(definitionFontId),
        marginLeft(marginLeft),
        marginTop(marginTop),
        renderPageBackground(renderPageBackground),
        layer(lookup.gloss.empty() ? Layer::Article : Layer::Brief) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  enum class Layer : uint8_t { Brief, Article };

  std::shared_ptr<Page> page;
  std::string headword;
  // "عرض + هم": the parts the lookup set aside to reach this headword. Empty when
  // the word was found as written. DictionaryStore fills it from the same
  // ArabicStem::segments the browser simulator draws its pills from.
  std::string decomposition;
  std::string definition;
  // Layer 1's text: the nearest thing to a plain meaning in the article.
  std::string gloss;
  // The other readings the word has, the answering one first. The reader steps
  // through them with the side buttons and opens one in place -- the article is a
  // read, not a second lookup, because each reading carries where its entry lives.
  std::vector<DictionaryReading> readings;
  // Which of them is highlighted, or -1 for none. -1 is a real position rather
  // than a state to press past: at rest Select opens the article, and either side
  // button lands on the line in one press.
  int readingIndex = -1;
  bool truncated = false;
  int readerFontId = 0;
  int definitionFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  bool renderPageBackground = true;
  Layer layer = Layer::Brief;
  std::vector<std::string> wrappedLines;
  // Row pitch varies line to line -- a line carrying Arabic is set from the taller
  // Arabic face -- so a page is a range of lines rather than a fixed count:
  // pageStarts[p] is the first line of page p, and lineHeights runs parallel to
  // wrappedLines.
  std::vector<int> lineHeights;
  // Which edge each line hangs off, decided once per paragraph rather than per line
  // so a bullet that mixes scripts does not alternate sides as it wraps. Parallel to
  // wrappedLines; 1 is right-aligned.
  std::vector<uint8_t> lineRtl;
  std::vector<int> pageStarts;
  int currentPage = 0;
  int totalPages = 1;

  Rect overlayRect() const;
  // What the current layer wraps and paginates: the gloss, or the whole article.
  const std::string& bodySource() const;
  // Height the "See also" block takes at the foot of the brief, 0 when the word
  // has only the one reading. Both the wrap (which keeps the gloss clear of it)
  // and the render (which draws it) have to agree.
  int seeAlsoHeight() const;
  // The readings other than the one being shown, in the order they were probed.
  std::vector<const DictionaryReading*> otherReadings() const;
  void moveReading(int step);
  // Swaps the brief onto the highlighted reading, reading its article off the
  // card. False when there is nothing highlighted or its bytes would not come
  // back, and the screen is left as it was.
  bool openHighlightedReading();
  // The "See also" block under the brief's gloss: the label on its own line, the
  // readings on the next, the highlighted one knocked out.
  void drawSeeAlso(const Rect& rect, int top) const;
  // Height the decomposition line takes under the title, 0 when there is none.
  // Both the wrap pass (which subtracts it from the body) and the render pass
  // (which puts the separator below it) have to agree.
  int decompositionHeight() const;
  void wrapText();
  void prepareDefinitionFontMetrics();
  int measureDefinitionText(const char* text) const;
  // Row pitch for the definition body. Both the wrap pass (which divides the
  // overlay by it) and the render pass (which steps rows by it) must use this
  // one value or the page they agree on stops matching the page drawn.
  int lineHeightFor(const std::string& line) const;
  void paginate(int bodyHeight);
  void pageLineRange(int page, int& begin, int& end) const;
  void prewarmVisibleDefinitionText() const;
};
