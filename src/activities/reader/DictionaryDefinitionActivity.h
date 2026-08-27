#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"

struct Rect;

class DictionaryDefinitionActivity final : public Activity {
 public:
  DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                               std::string headword, std::string definition, bool truncated, int readerFontId,
                               int definitionFontId, int marginLeft, int marginTop, bool renderPageBackground = true)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        page(std::move(page)),
        headword(std::move(headword)),
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
  std::string definition;
  bool truncated = false;
  int readerFontId = 0;
  int definitionFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  bool renderPageBackground = true;
  std::vector<std::string> wrappedLines;
  int currentPage = 0;
  int linesPerPage = 1;
  int totalPages = 1;

  Rect overlayRect() const;
  void wrapText();
  void prepareDefinitionFontMetrics();
  int measureDefinitionText(const char* text) const;
  // Row pitch for the definition body. Both the wrap pass (which divides the
  // overlay by it) and the render pass (which steps rows by it) must use this
  // one value or the page they agree on stops matching the page drawn.
  int definitionLineHeight() const;
  void prewarmVisibleDefinitionText() const;
};
