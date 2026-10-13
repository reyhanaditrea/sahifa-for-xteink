#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/SahifahMark.h"
#include "images/SahifahWordmark.h"

namespace {
// Boot design H: the open book in the khatam star, the SAHIFAH wordmark, and a
// framed progress bar over the status line. Margins follow the 480x800 design.
constexpr int SIDE = 36;
constexpr int BOTTOM = 36;
constexpr int BAR_HEIGHT = 18;  // 2px frame + 2px padding around a 10px fill
constexpr int BAR_INSET = 4;
constexpr int BAR_GAP = 12;

int barTop(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - BOTTOM - renderer.getLineHeight(SMALL_FONT_ID) - BAR_GAP - BAR_HEIGHT;
}
}  // namespace

void BootActivity::onEnter() {
  Activity::onEnter();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);

  // Mark, wordmark, then "Based on Midad", centred in the space above the bar.
  // drawImage blits whole bytes, so both bitmaps land on multiples of 8.
  constexpr int TOP = 82;
  const int groupHeight = SAHIFAHMARK_HEIGHT + 40 + SAHIFAHWORDMARK_HEIGHT + 10 + smallLine;
  const int markY = (TOP + (barTop(renderer) - TOP - groupHeight) / 2) / 8 * 8;
  const int wordY = markY + SAHIFAHMARK_HEIGHT + 40;

  renderer.clearScreen();
  renderer.drawImage(SahifahMark, (pageWidth - SAHIFAHMARK_WIDTH) / 2, markY, SAHIFAHMARK_WIDTH, SAHIFAHMARK_HEIGHT);
  // drawImage takes panel-oriented dimensions: in portrait the stored (rotated)
  // bitmap's width runs down the screen, so a non-square image passes them swapped.
  renderer.drawImage(SahifahWordmark, (pageWidth - SAHIFAHWORDMARK_WIDTH) / 2, wordY, SAHIFAHWORDMARK_HEIGHT,
                     SAHIFAHWORDMARK_WIDTH);
  renderer.drawCenteredText(SMALL_FONT_ID, wordY + SAHIFAHWORDMARK_HEIGHT + 10, tr(STR_BRAND_BY_FOULAD));

  const int top = barTop(renderer);
  renderer.drawRect(SIDE, top, pageWidth - 2 * SIDE, BAR_HEIGHT, 2, true);
  const int labelY = pageHeight - BOTTOM - smallLine;
  renderer.drawText(SMALL_FONT_ID, SIDE, labelY, tr(STR_BOOTING));
  renderer.drawText(SMALL_FONT_ID, pageWidth - SIDE - renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION), labelY,
                    CROSSPOINT_VERSION);
  showProgress(renderer, 1, 3);
}

void BootActivity::showProgress(const GfxRenderer& renderer, const int step, const int steps) {
  const int inner = renderer.getScreenWidth() - 2 * SIDE - 2 * BAR_INSET;
  renderer.fillRect(SIDE + BAR_INSET, barTop(renderer) + BAR_INSET, inner * step / steps, BAR_HEIGHT - 2 * BAR_INSET);
  renderer.displayBuffer();
}
