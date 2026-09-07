#pragma once

#include <Epub/Page.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"

struct Rect;

class DictionaryDefinitionActivity final : public Activity {
 public:
  DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                               std::string headword, std::string decomposition, std::string definition, bool truncated,
                               int readerFontId, int definitionFontId, int marginLeft, int marginTop,
                               bool renderPageBackground = true)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        page(std::move(page)),
        headword(std::move(headword)),
        decomposition(std::move(decomposition)),
        definition(std::move(definition)),
        truncated(truncated),
        readerFontId(readerFontId),
        definitionFontId(definitionFontId),
        marginLeft(marginLeft),
        marginTop(marginTop),
        renderPageBackground(renderPageBackground) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::shared_ptr<Page> page;
  std::string headword;
  // "عرض + هم": the parts the lookup set aside to reach this headword. Empty when
  // the word was found as written. DictionaryStore fills it from the same
  // ArabicStem::segments the browser simulator draws its pills from.
  std::string decomposition;
  std::string definition;
  bool truncated = false;
  int readerFontId = 0;
  int definitionFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  bool renderPageBackground = true;
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
