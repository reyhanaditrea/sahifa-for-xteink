#include "DictionarySuggestionsActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <ScriptDetector.h>

#include <algorithm>
#include <optional>
#include <string>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "DictionaryStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void DictionarySuggestionsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void DictionarySuggestionsActivity::lookupSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(suggestions.size())) return;
  // A suggestion is a headword the reader picked off a list; there is no word in
  // front of it to read as evidence.
  const auto lookup = DICTIONARIES.lookup(suggestions[selectedIndex], std::string(), false);
  if (lookup.status != DictionaryLookupResult::Status::Found) {
    const char* msg = lookup.status == DictionaryLookupResult::Status::LowMemory ? tr(STR_DICTIONARY_LOW_MEMORY)
                      : lookup.status == DictionaryLookupResult::Status::DecompressError
                          ? tr(STR_DICTIONARY_DECOMPRESS_ERROR)
                      : lookup.status == DictionaryLookupResult::Status::ReadError ? tr(STR_DICTIONARY_READ_ERROR)
                                                                                   : tr(STR_DEFINITION_NOT_FOUND);
    GUI.drawPopup(renderer, msg);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(700);
    requestUpdate();
    return;
  }

  startActivityForResult(std::make_unique<DictionaryDefinitionActivity>(
                             renderer, mappedInput, page, lookup, readerFontId,
                             DICTIONARIES.getDefinitionFontId(readerFontId), marginLeft, marginTop),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             setResult(ActivityResult{});
                             finish();
                             return;
                           }
                           requestUpdate();
                         });
}

void DictionarySuggestionsActivity::loop() {
  // Release edges, for the same reason as DictionaryDefinitionActivity::loop():
  // the whole dictionary stack has to agree on which edge it consumes, or the
  // unconsumed half lands on the activity underneath on the very next frame.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lookupSelected();
    return;
  }

  buttonNavigator.onNext([this] {
    if (!suggestions.empty()) {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(suggestions.size()));
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this] {
    if (!suggestions.empty()) {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(suggestions.size()));
      requestUpdate();
    }
  });
}

// These rows are dictionary content, not UI chrome: they are the same Arabic
// headwords the definition screen is about to show, so they follow the same
// Dictionary text size setting (Small / Large / Reading font) rather than the
// settings-row font. That is why this does not call GUI.drawList, which pins every
// row to UI_10_FONT_ID and takes its row pitch from a fixed metric -- neither of
// which can follow a font the user chose. Parameterising it would mean threading two
// arguments through both overrides (BaseTheme and LyraTheme, the latter being what
// FouladTheme actually inherits) and through every list in the firmware. Only
// rowTitle is ever needed here, so the parts of drawList that matter -- paging, the
// inverted selection, RTL anchoring -- are cheaper to keep than to generalise.
void DictionarySuggestionsActivity::drawSuggestionList(const Rect& rect) const {
  const int fontId = DICTIONARIES.getDefinitionFontId(readerFontId);
  const int lineHeight = std::max(1, renderer.getLineHeight(fontId));
  const int uiLineHeight = std::max(1, renderer.getLineHeight(UI_10_FONT_ID));
  // Same 8px of breathing room BaseMetrics gives a UI_10 list row, kept
  // proportional to whatever face is actually in use.
  const int rowHeight = lineHeight + 8;
  const int pageItems = std::max(1, rect.height / rowHeight);
  const int itemCount = static_cast<int>(suggestions.size());
  const int pageStart = selectedIndex / pageItems * pageItems;
  const int pageEnd = std::min(itemCount, pageStart + pageItems);

  // Prewarm every glyph about to be drawn in one pass: an SD-card reading font
  // faults each miss in individually otherwise, which is visible on the panel.
  if (auto* fcm = renderer.getFontCacheManager()) {
    std::string visible;
    visible.reserve(256);
    for (int i = pageStart; i < pageEnd; ++i) {
      if (!visible.empty()) visible.push_back('\n');
      visible += suggestions[i];
    }
    if (!visible.empty()) fcm->prewarmCache(fontId, visible.c_str(), 0x01);
  }

  const int padding = 10;
  const int textWidth = rect.width - padding * 2;
  for (int i = pageStart; i < pageEnd; ++i) {
    const int rowY = rect.y + (i - pageStart) * rowHeight;
    const std::string shown = renderer.truncatedText(fontId, suggestions[i].c_str(), textWidth);
    const bool arabic = ScriptDetector::containsArabic(shown.c_str());
    if (i == selectedIndex) {
      // Arabic descends further below the shared baseline than Latin does, so the
      // highlight grows downward or the tails get sliced -- the same correction the
      // theme's own drawList applies, for the same reason. drawList's own +4 is
      // calibrated for its one fixed row font (UI_10_FONT_ID); this list follows the
      // user's Dictionary text size setting instead, so the same correction has to
      // scale with lineHeight or a Large-font selection clips its descenders again.
      const int descenderPad = arabic ? (lineHeight * 4 + uiLineHeight - 1) / uiLineHeight : 0;
      renderer.fillRect(rect.x, rowY - 2, rect.width, rowHeight + descenderPad);
    }
    // Arabic anchors right whatever the UI language is: an English-UI device still
    // shows an Arabic candidate list. Glyph ordering is drawText's job; this is layout.
    const int x = (arabic || I18N.isRtl())
                      ? rect.x + rect.width - padding - renderer.getTextWidth(fontId, shown.c_str())
                      : rect.x + padding;
    renderer.drawText(fontId, x, rowY, shown.c_str(), i != selectedIndex);
  }

  if (itemCount > pageItems) {
    const std::string pageText =
        std::to_string(selectedIndex / pageItems + 1) + "/" + std::to_string((itemCount + pageItems - 1) / pageItems);
    const int width = renderer.getTextWidth(SMALL_FONT_ID, pageText.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - padding - width, rect.y + rect.height - 4, pageText.c_str());
  }
}

void DictionarySuggestionsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  std::optional<FontCacheManager::PrewarmScope> pageFontPrewarm;
  if (page) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      pageFontPrewarm.emplace(*fcm);
      // Our fork records fonts via the scan pass (render during an active
      // PrewarmScope), not vcodex's recordFontUsage/bionic params.
      page->render(renderer, readerFontId, marginLeft, marginTop);
      pageFontPrewarm->endScanAndPrewarm();
    }
    page->render(renderer, readerFontId, marginLeft, marginTop);
  }

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int overlayHeight = std::max((screenHeight * 3) / 4, 160);
  const Rect rect{10, screenHeight - overlayHeight - 10, screenWidth - 20, overlayHeight};

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 2, true);

  GUI.drawSubHeader(renderer, Rect{rect.x + 4, rect.y + 8, rect.width - 8, 40}, tr(STR_DID_YOU_MEAN),
                    originalWord.c_str());
  const int listTop = rect.y + 58;
  const int listHeight = rect.height - 68;
  if (suggestions.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + 20, tr(STR_NO_SUGGESTIONS));
  } else {
    drawSuggestionList(Rect{rect.x, listTop, rect.width, listHeight});
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
