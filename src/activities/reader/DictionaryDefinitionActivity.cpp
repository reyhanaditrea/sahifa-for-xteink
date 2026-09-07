#include "DictionaryDefinitionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <ScriptDetector.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_WRAPPED_DEFINITION_LINES = 384;

// drawText routes a whole string to the Arabic path as soon as it holds one Arabic
// codepoint: every glyph comes from the Arabic face and the line is laid out RTL.
// A line that mixes scripts therefore cannot be drawn in one call -- an English
// sentence quoting an Arabic word came out in the Arabic face, backwards.
//
// So a line is drawn as runs instead: each run is single-script and goes to the
// renderer on its own, and the paragraph's direction decides the order they are
// placed in. That is the part of the bidi algorithm this screen actually needs --
// within a run the renderer already does the right thing (the Arabic path shapes
// and reverses; the Latin path runs left to right), and the dictionary never nests
// a direction deeper than one level.
//
// NEUTRAL tokens -- bare punctuation, digits, reference numbers, the "·" between
// the principal parts -- carry no direction of their own and join whichever run is
// open, so "ذهب, he went" is two runs rather than three.
enum class TokenScript { NEUTRAL, ARABIC, LATIN };

// Only a letter gives a token a direction. Treating every byte over 0x7F as Latin
// made the separators the dictionary itself uses -- "·", en/em dashes, curly quotes
// -- break the Arabic run they sit in, so one headword line came out as five.
bool isLatinLetter(const uint32_t cp) {
  if (cp < 0x80) return std::isalpha(static_cast<int>(cp)) != 0;
  if (cp == 0x00D7 || cp == 0x00F7) return false;  // multiplication/division signs
  return (cp >= 0x00C0 && cp <= 0x024F) ||         // Latin-1 letters + Latin Extended-A/B
         (cp >= 0x0370 && cp <= 0x03FF) ||         // Greek
         (cp >= 0x0400 && cp <= 0x04FF);           // Cyrillic
}

TokenScript tokenScript(const std::string& token) {
  if (ScriptDetector::containsArabic(token.c_str())) return TokenScript::ARABIC;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(token.c_str());
  while (*p) {
    uint32_t cp = *p;
    size_t extra = 0;
    if (cp >= 0xF0) {
      cp &= 0x07;
      extra = 3;
    } else if (cp >= 0xE0) {
      cp &= 0x0F;
      extra = 2;
    } else if (cp >= 0xC0) {
      cp &= 0x1F;
      extra = 1;
    }
    ++p;
    for (size_t i = 0; i < extra && (*p & 0xC0) == 0x80; ++i, ++p) cp = (cp << 6) | (*p & 0x3F);
    if (isLatinLetter(cp)) return TokenScript::LATIN;
  }
  return TokenScript::NEUTRAL;
}

// The direction of a paragraph is the direction of its first letter that has one
// (Unicode's own rule, UBA P2/P3), and every line the paragraph wraps to keeps it.
bool paragraphIsRtl(const std::string& line) {
  size_t pos = 0;
  while (pos < line.size()) {
    while (pos < line.size() && line[pos] == ' ') ++pos;
    const size_t start = pos;
    while (pos < line.size() && line[pos] != ' ') ++pos;
    if (start == pos) break;
    const TokenScript script = tokenScript(line.substr(start, pos - start));
    if (script != TokenScript::NEUTRAL) return script == TokenScript::ARABIC;
  }
  return false;
}

// U+2022, and the gap the dictionary writes after it.
constexpr const char* BULLET = "\xE2\x80\xA2";
constexpr const char* BULLET_PREFIX = "\xE2\x80\xA2  ";

// "• text" -> "•  text". One space sets the marker too close to read as a marker;
// the wrap keeps a double space (a run carries its own spaces), so widening it here
// is enough and the continuation indent is measured from the same string.
std::string openBulletGap(const std::string& line) {
  size_t pos = 0;
  while (pos < line.size() && line[pos] == ' ') ++pos;
  const size_t bulletLen = std::strlen(BULLET);
  if (line.compare(pos, bulletLen, BULLET) != 0) return line;
  const size_t after = pos + bulletLen;
  if (after >= line.size() || line[after] != ' ') return line;
  if (after + 1 < line.size() && line[after + 1] == ' ') return line;
  return line.substr(0, after) + ' ' + line.substr(after);
}

struct TextRun {
  std::string text;
  int leadingSpaces = 0;  // spaces between this run and the one before it
};

// One wrapped line -> the single-script pieces it is drawn from, in logical order.
// The line's own indent stays inside the first run so it still indents; every other
// gap becomes leadingSpaces, because the space between two runs belongs to neither
// and has to be measured in the Latin face whichever scripts it separates.
std::vector<TextRun> splitRuns(const std::string& line) {
  std::vector<TextRun> runs;
  TokenScript runScript = TokenScript::NEUTRAL;
  size_t pos = 0;
  while (pos < line.size()) {
    const size_t spaceStart = pos;
    while (pos < line.size() && line[pos] == ' ') ++pos;
    const int spaces = static_cast<int>(pos - spaceStart);
    const size_t start = pos;
    while (pos < line.size() && line[pos] != ' ') ++pos;
    if (start == pos) break;
    const std::string token = line.substr(start, pos - start);
    const TokenScript script = tokenScript(token);
    const bool breaks =
        !runs.empty() && script != TokenScript::NEUTRAL && runScript != TokenScript::NEUTRAL && script != runScript;
    if (runs.empty() || breaks) {
      runs.push_back(TextRun{runs.empty() ? line.substr(0, start) + token : token, runs.empty() ? 0 : spaces});
      runScript = script;
      continue;
    }
    runs.back().text.append(static_cast<size_t>(spaces), ' ');
    runs.back().text += token;
    if (script != TokenScript::NEUTRAL) runScript = script;
  }
  return runs;
}
}  // namespace

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  prepareDefinitionFontMetrics();
  wrapText();
  requestUpdate();
}

void DictionaryDefinitionActivity::onExit() {
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  Activity::onExit();
}

Rect DictionaryDefinitionActivity::overlayRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int margin = std::max(8, metrics.contentSidePadding / 2);
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int height = std::max((screenHeight * 3) / 4, 160);
  return Rect{margin, screenHeight - height - margin, screenWidth - margin * 2, height};
}

void DictionaryDefinitionActivity::prepareDefinitionFontMetrics() {
  if (!renderer.isSdCardFont(definitionFontId)) return;

  if (!definition.empty()) {
    renderer.ensureSdCardFontReady(definitionFontId, definition.c_str(), 0x01);
  }

  if (truncated) {
    const std::string marker = std::string("[") + tr(STR_DEFINITION_TRUNCATED) + "]";
    renderer.ensureSdCardFontReady(definitionFontId, marker.c_str(), 0x01);
  }
}

int DictionaryDefinitionActivity::decompositionHeight() const {
  if (decomposition.empty()) return 0;
  // Arabic is drawn from its own resolved face, which runs taller than the Latin
  // font the row is nominally sized from.
  const int latin = renderer.getLineHeight(SMALL_FONT_ID);
  return std::max(latin, renderer.getLineHeight(renderer.getResolvedArabicFontId(SMALL_FONT_ID))) + 4;
}

int DictionaryDefinitionActivity::measureDefinitionText(const char* text) const {
  // Each run is measured in the face it will be drawn from -- measuring a mixed
  // string in one call puts the whole thing through the Arabic face and the wrap
  // then fills lines against a width no line actually has.
  int width = 0;
  const int space = renderer.getTextAdvanceX(definitionFontId, " ", EpdFontFamily::REGULAR);
  for (const auto& run : splitRuns(text == nullptr ? std::string() : std::string(text))) {
    width += run.leadingSpaces * space;
    width += renderer.getTextAdvanceX(definitionFontId, run.text.c_str(), EpdFontFamily::REGULAR);
  }
  return width;
}

int DictionaryDefinitionActivity::lineHeightFor(const std::string& line) const {
  const int latin = renderer.getLineHeight(definitionFontId);
  int pitch = latin;

  if (line.empty()) {
    // A blank source line is a paragraph break, not a row of text -- it only has to
    // read as a gap. Giving it a full row of the tallest pitch on the page is what
    // padded entries out to roughly twice the height they needed.
    pitch = latin / 2;
  } else if (ScriptDetector::containsArabic(line.c_str())) {
    // Arabic is drawn from its own resolved face, not the Latin one the overlay
    // takes its pitch from. With the Reading-font definition size those differ
    // sharply -- Noto Naskh advances 43 where the Latin font it resolves from
    // advances 30 -- so rows drawn at the Latin pitch collided.
    pitch = std::max(latin, renderer.getLineHeight(renderer.getResolvedArabicFontId(definitionFontId)));

    // advanceY reserves room for the ascender and descender, and tashkeel sit
    // outside both, so an Arabic face set solid still reads cramped. This is the
    // whole story at the UI tiers, where the Arabic face is chosen precisely to
    // match Latin line height (Tajawal and Inter both advance 25 at 10pt) and the
    // max() above therefore changes nothing.
    // ponytail: flat fraction, not a per-font tashkeel extent -- the font data
    // carries no mark-height metric to compute one from.
    pitch += pitch / 6;
  }

  // Definition text is body text, so it follows the reader's own line-spacing
  // setting rather than the raw font advance. Nothing outside the book layout
  // consulted this before.
  return std::max(1, static_cast<int>(pitch * SETTINGS.getReaderLineCompression() + 0.5f));
}

void DictionaryDefinitionActivity::paginate(const int bodyHeight) {
  lineHeights.clear();
  lineHeights.reserve(wrappedLines.size());
  for (const auto& line : wrappedLines) lineHeights.push_back(lineHeightFor(line));

  // Measured once, here, over the final line list -- the truncation marker is
  // appended before this runs, so no line can land on a page it was not counted for.
  pageStarts.clear();
  pageStarts.push_back(0);
  int used = 0;
  for (size_t i = 0; i < lineHeights.size(); ++i) {
    // used > 0 guarantees forward progress: a single line taller than the body gets
    // its own page rather than starting an empty one forever.
    if (used > 0 && used + lineHeights[i] > bodyHeight) {
      pageStarts.push_back(static_cast<int>(i));
      used = 0;
    }
    used += lineHeights[i];
  }
  totalPages = static_cast<int>(pageStarts.size());
}

void DictionaryDefinitionActivity::pageLineRange(const int page, int& begin, int& end) const {
  if (pageStarts.empty()) {
    begin = end = 0;
    return;
  }
  const int last = static_cast<int>(pageStarts.size()) - 1;
  const int p = std::clamp(page, 0, last);
  begin = pageStarts[p];
  end = p < last ? pageStarts[p + 1] : static_cast<int>(wrappedLines.size());
}

void DictionaryDefinitionActivity::prewarmVisibleDefinitionText() const {
  auto* fcm = renderer.getFontCacheManager();
  if (!fcm) return;

  int begin = 0, end = 0;
  pageLineRange(currentPage, begin, end);
  std::string visibleText;
  visibleText.reserve(512);
  for (int i = begin; i < end; ++i) {
    if (!visibleText.empty()) visibleText.push_back('\n');
    visibleText += wrappedLines[i];
  }

  if (!visibleText.empty()) {
    fcm->prewarmCache(definitionFontId, visibleText.c_str(), 0x01);
  }
}

void DictionaryDefinitionActivity::wrapText() {
  wrappedLines.clear();
  lineRtl.clear();
  bool wrapLimitReached = false;
  const Rect rect = overlayRect();
  const int padding = 10;
  const int maxWidth = std::max(80, rect.width - padding * 2);
  const int headerHeight = renderer.getLineHeight(UI_10_FONT_ID) + 20 + decompositionHeight();
  const int footerHeight = renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int bodyHeight = std::max(1, rect.height - headerHeight - footerHeight - padding);

  auto utf8UnitLength = [](const unsigned char c) -> size_t {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
  };

  auto trimTrailingSpaces = [](std::string& line) {
    while (!line.empty() && line.back() == ' ') line.pop_back();
  };

  // Which side of the box the paragraph hangs off. Splitting a mixed line by script
  // keeps each line drawable, but it also lets an English sentence quoting Arabic
  // alternate sides down the page -- the English flush left, the quoted words flush
  // right, the rest left again. Unicode settles this the same way for a paragraph
  // (UBA P2/P3): the first letter with a direction of its own decides, and every
  // line the paragraph wraps to keeps that side. So a bullet reads as one block.
  bool paragraphRtl = false;

  auto appendWrappedLine = [&](std::string line) {
    if (wrappedLines.size() >= MAX_WRAPPED_DEFINITION_LINES) {
      wrapLimitReached = true;
      return false;
    }
    wrappedLines.push_back(std::move(line));
    lineRtl.push_back(paragraphRtl ? 1 : 0);
    return true;
  };

  auto expandTabs = [](const std::string& line) {
    std::string expanded;
    expanded.reserve(line.size());
    for (const char c : line) {
      if (c == '\t') {
        expanded += "  ";
      } else {
        expanded.push_back(c);
      }
    }
    return expanded;
  };

  // How many spaces stand in for the bullet and the gap after it. Measured rather
  // than guessed: the bullet is drawn from whichever face the line resolves to, and
  // a space is a good deal narrower than it at every definition size.
  const int spaceAdvance = std::max(1, renderer.getTextAdvanceX(definitionFontId, " ", EpdFontFamily::REGULAR));
  const int bulletAdvance = renderer.getTextAdvanceX(definitionFontId, BULLET_PREFIX, EpdFontFamily::REGULAR);
  const size_t bulletIndent = std::max<size_t>(3, (bulletAdvance + spaceAdvance - 1) / spaceAdvance);

  auto continuationPrefixFor = [bulletIndent](const std::string& line, const std::string& prefix) {
    size_t pos = prefix.size();
    if (pos + 1 < line.size() && (line[pos] == '-' || line[pos] == '*' || line[pos] == '+') && line[pos + 1] == ' ') {
      return prefix + "  ";
    }
    // A wrapped sense hangs under its own text, not under the bullet.
    if (line.compare(pos, std::strlen(BULLET), BULLET) == 0) {
      return prefix + std::string(bulletIndent, ' ');
    }

    const size_t numberStart = pos;
    while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) ++pos;
    if (pos > numberStart && pos + 1 < line.size() && (line[pos] == '.' || line[pos] == ')') && line[pos + 1] == ' ') {
      return prefix + std::string(pos + 2 - prefix.size(), ' ');
    }
    return prefix;
  };

  auto appendLongToken = [&](const std::string& token, std::string& currentLine, std::string activePrefix,
                             const std::string& continuationPrefix, bool& hasContent) {
    for (size_t pos = 0; pos < token.size();) {
      const size_t unitLen = std::min(utf8UnitLength(static_cast<unsigned char>(token[pos])), token.size() - pos);
      const std::string unit = token.substr(pos, unitLen);
      const std::string test = currentLine + unit;
      if (currentLine.size() > activePrefix.size() && measureDefinitionText(test.c_str()) > maxWidth) {
        trimTrailingSpaces(currentLine);
        if (!appendWrappedLine(currentLine)) return;
        activePrefix = continuationPrefix;
        currentLine = activePrefix;
        hasContent = false;
        continue;
      }
      currentLine = test;
      hasContent = true;
      pos += unitLen;
    }
  };

  auto wrapSourceLine = [&](std::string sourceLine) {
    if (wrapLimitReached) return;
    sourceLine = expandTabs(sourceLine);
    sourceLine = openBulletGap(sourceLine);
    trimTrailingSpaces(sourceLine);
    if (sourceLine.empty()) {
      appendWrappedLine(std::string());
      return;
    }

    size_t firstText = 0;
    while (firstText < sourceLine.size() && sourceLine[firstText] == ' ') ++firstText;
    const std::string firstPrefix = sourceLine.substr(0, std::min<size_t>(firstText, 4));
    const std::string continuationPrefix = continuationPrefixFor(sourceLine, firstPrefix);

    paragraphRtl = paragraphIsRtl(sourceLine);

    std::string currentLine = firstPrefix;
    bool hasContent = false;
    size_t pos = firstText;
    while (pos < sourceLine.size()) {
      size_t spaces = 0;
      while (pos < sourceLine.size() && sourceLine[pos] == ' ') {
        ++spaces;
        ++pos;
      }
      if (pos >= sourceLine.size()) break;

      const size_t tokenStart = pos;
      while (pos < sourceLine.size() && sourceLine[pos] != ' ') ++pos;
      const std::string token = sourceLine.substr(tokenStart, pos - tokenStart);

      const std::string separator = hasContent ? (spaces > 1 ? "  " : " ") : "";
      const std::string test = currentLine + separator + token;
      if (measureDefinitionText(test.c_str()) <= maxWidth) {
        currentLine = test;
        hasContent = true;
        continue;
      }

      if (hasContent) {
        trimTrailingSpaces(currentLine);
        if (!appendWrappedLine(currentLine)) return;
        currentLine = continuationPrefix;
        hasContent = false;
      }

      const std::string prefixedToken = currentLine + token;
      if (measureDefinitionText(prefixedToken.c_str()) <= maxWidth) {
        currentLine = prefixedToken;
        hasContent = true;
      } else {
        appendLongToken(token, currentLine, currentLine, continuationPrefix, hasContent);
      }
    }

    trimTrailingSpaces(currentLine);
    if (hasContent || currentLine.size() > firstPrefix.size()) {
      appendWrappedLine(currentLine);
    } else {
      appendWrappedLine(std::string());
    }
  };

  size_t lineStart = 0;
  for (size_t i = 0; i <= definition.size(); ++i) {
    if (i == definition.size() || definition[i] == '\n') {
      wrapSourceLine(definition.substr(lineStart, i - lineStart));
      if (wrapLimitReached) break;
      lineStart = i + 1;
    }
  }

  if (truncated || wrapLimitReached) {
    const std::string marker = std::string("[") + tr(STR_DEFINITION_TRUNCATED) + "]";
    if (wrappedLines.size() < MAX_WRAPPED_DEFINITION_LINES) {
      wrappedLines.push_back(marker);
      lineRtl.push_back(0);
    } else if (!wrappedLines.empty()) {
      wrappedLines.back() = marker;
      lineRtl.back() = 0;
    }
  }
  paginate(bodyHeight);
  currentPage = std::clamp(currentPage, 0, totalPages - 1);
}

void DictionaryDefinitionActivity::loop() {
  const bool prevPage = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextPage = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Right);

  if (prevPage && currentPage > 0) {
    --currentPage;
    requestUpdate();
    return;
  }
  if (nextPage && currentPage + 1 < totalPages) {
    ++currentPage;
    requestUpdate();
    return;
  }
  // Release edges, not press edges. Button edges live for exactly one
  // InputManager::update(), and finishing on the press leaves the matching
  // release for whoever is current on the next frame -- which, now that the
  // dictionary stays open, is DictionaryWordSelectActivity acting on releases:
  // Done re-looked-up the same word forever, Back closed the whole mode.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(ActivityResult{});
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }
}

void DictionaryDefinitionActivity::render(RenderLock&&) {
  if (renderPageBackground) {
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
  }

  const Rect rect = overlayRect();
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 2, true);

  const int padding = 10;
  const int titleY = rect.y + padding;
  const int titleMaxWidth = rect.width - padding * 2 - 54;
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, headword.c_str(), titleMaxWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  if (totalPages > 1) {
    const std::string pageText = std::to_string(currentPage + 1) + "/" + std::to_string(totalPages);
    const int pageWidth = renderer.getTextWidth(SMALL_FONT_ID, pageText.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - padding - pageWidth, titleY + 2, pageText.c_str());
  }

  if (!decomposition.empty()) {
    // Layer 1 of the lookup, under the headword it produced: the reader sees that
    // عَرَضَهُمۡ was read as عرض plus a pronoun, so a surprising article is traceable
    // to the split rather than a mystery.
    const std::string line =
        renderer.truncatedText(SMALL_FONT_ID, decomposition.c_str(), rect.width - padding * 2, EpdFontFamily::REGULAR);
    renderer.drawTextInWidth(SMALL_FONT_ID, rect.x + padding, titleY + renderer.getLineHeight(UI_10_FONT_ID) + 2,
                             rect.width - padding * 2, line.c_str());
  }

  const int separatorY = titleY + renderer.getLineHeight(UI_10_FONT_ID) + 12 + decompositionHeight();
  renderer.drawLine(rect.x + padding, separatorY, rect.x + rect.width - padding - 1, separatorY, true);

  const int bodyY = separatorY + 10;
  const int bodyWidth = rect.width - padding * 2;
  int begin = 0, end = 0;
  pageLineRange(currentPage, begin, end);
  prewarmVisibleDefinitionText();
  int y = bodyY;
  for (int i = begin; i < end; ++i) {
    // Not drawTextInWidth: that draws the line in one call and aligns it by its own
    // script, which put an English bullet quoting Arabic in the Arabic face and hung
    // consecutive lines of one bullet off opposite edges. Each run goes to the
    // renderer on its own instead, placed in the paragraph's direction: logical
    // order from the left edge, or the same order laid down from the right.
    const bool rtl = i < static_cast<int>(lineRtl.size()) && lineRtl[i] != 0;
    const int space = renderer.getTextAdvanceX(definitionFontId, " ", EpdFontFamily::REGULAR);
    int cursor = rtl ? rect.x + padding + bodyWidth : rect.x + padding;
    for (const auto& run : splitRuns(wrappedLines[i])) {
      const int runWidth = renderer.getTextAdvanceX(definitionFontId, run.text.c_str(), EpdFontFamily::REGULAR);
      if (rtl) {
        cursor -= run.leadingSpaces * space + runWidth;
        renderer.drawText(definitionFontId, cursor, y, run.text.c_str());
      } else {
        cursor += run.leadingSpaces * space;
        renderer.drawText(definitionFontId, cursor, y, run.text.c_str());
        cursor += runWidth;
      }
    }
    y += lineHeights[i];
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
