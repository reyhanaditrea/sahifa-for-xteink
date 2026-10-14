#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
LEXENDDECA_FONT_SIZES=(12 14 16 18)
NOTOSANS_FONT_SIZES=(12 14 16 18)

# Bitter was the built-in Latin reading serif until its glyph data (~778KB) was
# moved to the Manage Fonts catalog; Lexend Deca is the built-in now. The TTF stays
# in source/ because Tasbih and Stopwatch below still cut their display digits from
# Bitter-Bold -- only the reading-size generation is gone.

# Tasbih counter digits: a genuinely large (32pt) display size for the Tasbih
# app's counter, which none of the reader font sizes above go up to. Digit-
# only (--script none --additional-intervals 0x30,0x39 = '0'-'9') keeps this
# cheap in flash (10 glyphs, ~1.3KB compressed) despite the large point size --
# reuses the same Bitter-Bold source as the reading font above.
python fontconvert.py tasbih_32_bold 32 ../builtinFonts/source/Bitter/Bitter-Bold.ttf --2bit --compress \
  --script none --additional-intervals 0x30,0x39 > ../builtinFonts/tasbih_32_bold.h
echo "Generated ../builtinFonts/tasbih_32_bold.h"

# Stop Watch counter digits: same idea as the Tasbih font above, but also
# needs a colon for "MM:SS" -- '0'-':' (0x3A immediately follows '9'=0x39 in
# ASCII) is one contiguous range, so a single --additional-intervals covers
# both digits and the separator.
python fontconvert.py stopwatch_32_bold 32 ../builtinFonts/source/Bitter/Bitter-Bold.ttf --2bit --compress \
  --script none --additional-intervals 0x30,0x3A > ../builtinFonts/stopwatch_32_bold.h
echo "Generated ../builtinFonts/stopwatch_32_bold.h"

# Lexend Deca (OFL, google/fonts ofl/lexenddeca): second built-in Latin reading
# option, a sans engineered against reading-fluency research (visual crowding),
# same anti-aliasing reasoning as Bitter above. Static instance pinned at
# wght=400/700. No italic master exists for this typeface at all (confirmed:
# google/fonts ofl/lexenddeca ships exactly one variable file, no italic
# counterpart) -- EpdFontFamily::getFont() falls back to regular/bold for
# italic/bold-italic requests when those slots are null, so this is a
# non-issue, not a missing conversion step.
for size in ${LEXENDDECA_FONT_SIZES[@]}; do
  for style in "Regular" "Bold"; do
    font_name="lexenddeca_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/LexendDeca/LexendDeca-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
# Medium, not Regular, is the UI text weight: 1-bit rasterisation at these sizes
# snaps stems to whole pixels, and Regular lands on 2px where Medium lands on 3.
UI_FONT_STYLES=("Medium" "Bold")

python generate-ui-noto-fonts.py

# Inter (OFL, google/fonts ofl/inter): UI font, replacing Ubuntu -- improved
# readability at small sizes, per the same rationale CrossInk documents.
# Deliberately NOT --2bit/--compress/--pnum: Ubuntu never used them either
# (1-bit, uncompressed -- crisp UI chrome with no grayscale AA, unlike the
# 2-bit+AA READING fonts above), and this swap is scoped to the typeface
# only, not the rendering format. No Hebrew/Vietnamese fallback stacking
# here (Ubuntu needed it): this fork ships EN+AR UI only (see
# lib/I18n/translations/), so Hebrew coverage is never exercised; Vietnamese
# tone marks (U+1EA0-U+1EF9) are natively covered by Inter itself (confirmed:
# 90/90 codepoints via fontTools' cmap), unlike Ubuntu, which needed the
# Vietnamese-cut fallback for that block. Static instance pinned at
# wght=400/700 from the variable font.
for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="inter_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Inter/Inter-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path > $output_path
    echo "Generated $output_path"
  done
done

# must cover the *output* of ArabicShaper's contextual shaping (isolated/
# initial/medial/final forms and Lam-Alef ligatures) -- not base letters, or
# shaped UI text silently drops glyphs.
# Curated for firmware-size budget: core Arabic (Presentation Forms-B,
# incl. the Lam-Alef ligature forms) plus the Farsi/Urdu extra letters'
# Presentation Forms-A blocks, the few characters shaping leaves at their
# base codepoint, Arabic punctuation, and both digit sets. No harakat and
# no Sindhi/Pashto/Kurdish forms — book text gets those from SD-card fonts.
ARABIC_INTERVALS=(
  --additional-intervals "0x060C,0x060C"  # Arabic comma
  --additional-intervals "0x061B,0x061B"  # Arabic semicolon
  --additional-intervals "0x061F,0x061F"  # Arabic question mark
  --additional-intervals "0x0621,0x0621"  # hamza (non-joining, never shaped)
  --additional-intervals "0x0640,0x0640"  # tatweel
  --additional-intervals "0x0660,0x0669"  # Arabic-Indic digits
  --additional-intervals "0x06BA,0x06BA"  # noon ghunna base (initial/medial keep base cp)
  --additional-intervals "0x06D4,0x06D4"  # Urdu full stop
  --additional-intervals "0x06F0,0x06F9"  # extended Arabic-Indic digits (Farsi/Urdu)
  --additional-intervals "0xFB56,0xFB59"  # peh (Farsi)
  --additional-intervals "0xFB66,0xFB69"  # tteh (Urdu)
  --additional-intervals "0xFB7A,0xFB7D"  # tcheh (Farsi)
  --additional-intervals "0xFB88,0xFB95"  # ddal, jeh, rreh (Urdu), keheh, gaf (Farsi/Urdu)
  --additional-intervals "0xFB9E,0xFB9F"  # noon ghunna isolated/final (Urdu)
  --additional-intervals "0xFBA6,0xFBB1"  # heh goal, heh doachashmee, yeh barree(+hamza) (Urdu)
  --additional-intervals "0xFBFC,0xFBFF"  # farsi yeh (Farsi/Urdu)
  --additional-intervals "0xFE80,0xFEFC"  # Presentation Forms-B: core Arabic + Lam-Alef
)

python fontconvert.py notosans_8_regular 8 \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf \
  ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf \
  ../builtinFonts/source/NotoSansArabic/NotoSansArabic-Regular.ttf \
  --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" > ../builtinFonts/notosans_8_regular.h

# All three built-in Arabic fonts below have OpenType GPOS MarkBasePos/MarkMarkPos
# tables (confirmed via fontTools) -- they anchor tashkeel to their base letter
# through GPOS, which this repo's FreeType-based single-glyph rasterizer never
# runs. Without --reposition-marks each mark glyph rasterizes at its own raw,
# GPOS-relative origin instead of a sensible fixed position, which on real
# hardware showed up as tashkeel intermittently missing/misplaced (some raw
# offsets happened to land somewhere visible, most didn't). Keep this flag on
# every Arabic font conversion below, not just the ones that visibly needed it.

# Dedicated built-in Arabic font (ArabicFontSystem's zero-setup default), bundled at
# the three sizes actually used for Arabic-eligible UI text (book titles, authors,
# filenames, chapter titles) -- SMALL_FONT_ID=8pt, UI_10_FONT_ID=10pt,
# UI_12_FONT_ID=12pt (see fontIds.h). GfxRenderer::drawArabicText/getArabicTextWidth
# pick whichever of these three matches the caller's requested fontId, so Arabic text
# renders at the same size/baseline as the surrounding Latin text instead of a single
# fixed size that overflows small rows or grid cells. --script arabic swaps
# fontconvert.py's default Latin/Cyrillic interval set for a minimal one sized for
# this font alone (basic Latin + Arabic blocks only).
ARABIC_UI_FONT_SIZES=(8 10 12)
for size in ${ARABIC_UI_FONT_SIZES[@]}; do
  python fontconvert.py notosansarabic_${size}_regular ${size} \
    ../builtinFonts/source/NotoSansArabic/NotoSansArabic-Regular.ttf \
    --2bit --compress --script arabic --reposition-marks > ../builtinFonts/notosansarabic_${size}_regular.h
  echo "Generated ../builtinFonts/notosansarabic_${size}_regular.h"
done

# Built-in Arabic READING fonts (ArabicFontSystem's default reading families) at the
# four reading sizes -- see kBuiltinArabicReadingFontIds. Noto Naskh briefly moved to
# the Manage Fonts catalog to save flash and came straight back: a reader built around
# Arabic that ships no Arabic BOOK face, and puts one behind a sign-in and a phone app,
# is not one. Tajawal is a UI sans and reads like one at paragraph length.
ARABIC_READING_FONT_SIZES=(12 14 16 18)
for size in ${ARABIC_READING_FONT_SIZES[@]}; do
  python fontconvert.py notonaskharabic_${size}_regular ${size} \
    ../builtinFonts/source/NotoNaskhArabic/NotoNaskhArabic-Regular.ttf \
    --2bit --compress --script arabic --reposition-marks > ../builtinFonts/notonaskharabic_${size}_regular.h
  echo "Generated ../builtinFonts/notonaskharabic_${size}_regular.h"
done

# Tajawal (Boutros International, OFL-licensed, google/fonts ofl/tajawal): a modern
# geometric-sans reading option alongside the two traditional book-printing styles
# above. Has GPOS MarkBasePos/MarkMarkPos (confirmed via fontTools), same as the
# other three Arabic fonts here -- --reposition-marks required.
for size in ${ARABIC_READING_FONT_SIZES[@]}; do
  python fontconvert.py tajawal_${size}_regular ${size} \
    ../builtinFonts/source/Tajawal/Tajawal-Regular.ttf \
    --2bit --compress --script arabic --reposition-marks > ../builtinFonts/tajawal_${size}_regular.h
  echo "Generated ../builtinFonts/tajawal_${size}_regular.h"
done
# Tajawal also doubles as the Arabic UI-chrome font (headers, button hints, list
# rows -- SMALL_FONT_ID=8pt, UI_10_FONT_ID=10pt, UI_12_FONT_ID=12pt), replacing
# NotoSansArabic there so the whole interface reads in one consistent Arabic
# typeface instead of mixing Tajawal (reading) with NotoSansArabic (chrome). Only
# 8/10pt need generating here -- the reading loop above already produced
# tajawal_12_regular.h with identical conversion flags, so UI_12_FONT_ID reuses
# that file directly (see ArabicFontSystem.cpp's applyArabicMappings).
for size in 8 10; do
  python fontconvert.py tajawal_${size}_regular ${size} \
    ../builtinFonts/source/Tajawal/Tajawal-Regular.ttf \
    --2bit --compress --script arabic --reposition-marks > ../builtinFonts/tajawal_${size}_regular.h
  echo "Generated ../builtinFonts/tajawal_${size}_regular.h"
done
# KFGQPC Uthmanic Hafs: the Madinah Mushaf's own typeface, the Quran's default
# reading font (see QuranBook::ensureExtracted / kBuiltinArabicReadingFontIds).
# --shape-fallback: this font only exposes Arabic Presentation Forms via GSUB
# (no cmap entries), which our renderer doesn't run -- see fontconvert.py's
# --shape-fallback help text for the full rationale.
# --contrast-gamma 0.3: this font's fine calligraphic strokes anti-alias into
# mostly-gray coverage at reading sizes (confirmed: only ~45% of a representative
# glyph's ink pixels are near-black before this), which then quantizes to light
# gray under --2bit -- "faded" per user report against a real Mushaf photo. 0.3
# chosen by rendering the actual post-quantization pixel grid at several gamma
# values side by side: 0.45 was a visible improvement but still gray-heavy, 0.2
# started flattening the calligraphic stroke shape into a solid blob, 0.3 gave a
# solidly black stroke while keeping the letterform's shape intact. Not applied
# to the other Arabic fonts above; they weren't reported as faded.
# Own sizes, larger than ARABIC_READING_FONT_SIZES: see kBuiltinArabicReadingFontIds.
for size in 11 16 18 22 26; do
  python fontconvert.py uthmanichafs_${size}_regular ${size} \
    ../builtinFonts/source/UthmanicHafs/UthmanicHafs_V22.ttf \
    --2bit --compress --script arabic --reposition-marks --shape-fallback \
    --contrast-gamma 0.3 > ../builtinFonts/uthmanichafs_${size}_regular.h
  echo "Generated ../builtinFonts/uthmanichafs_${size}_regular.h"
done

# QuranCommon: tiny 8-glyph font carrying only the real Bismillah ligature
# (U+FDFD, ARABIC LIGATURE BISMILLAH AR-RAHMAN AR-RAHEEM) via a direct cmap
# entry -- see GfxRenderer::setBismillahFontId. Single fixed 18pt size only,
# not one-per-reading-tier: the glyph is a whole vocalized phrase baked into
# one wide glyph (241px at 18pt), and EpdGlyph::width is a uint8_t (255px
# cap) -- any size above ~18pt pushes the glyph past that ceiling, which
# silently drops the font's only glyph and crashes fontconvert.py's
# compression-stats printout on a divide-by-zero. 18pt is the largest size
# that stays safely under the cap, so every reading tier uses this one font.
# --script none: this font has a handful of incidental ASCII cmap entries
# (used for an unrelated GSUB trigger in the source project) that --script
# none excludes, keeping only the Bismillah codepoint via
# --additional-intervals.
python fontconvert.py quran_common_18_regular 18 \
  ../builtinFonts/source/QuranCommon/quran-common.ttf \
  --2bit --compress --script none --additional-intervals 0xFDFD,0xFDFD \
  > ../builtinFonts/quran_common_18_regular.h
echo "Generated ../builtinFonts/quran_common_18_regular.h"

# Surah banner: 114 calligraphic surah-name glyphs + 1 shared ornament glyph baked
# from surah-name-v4.ttf, which has no cmap entries at all -- see
# source/SurahNameV4/NOTICE.md and gen_surah_banner_glyphmap.py for why this needs
# --glyph-map (HarfBuzz-shape a per-surah ASCII trigger string, bake the resulting
# glyphs under dedicated PUA codepoints) instead of a normal cmap-driven export.
# Single fixed 24pt size, not one per reading tier: this is a once-per-surah
# chrome element, not line-by-line reading text, and even 114 glyphs' worth of
# calligraphy stays well under EpdGlyph's uint8_t width/height cap at this size
# (confirmed max 69x36px at 18pt during design, with headroom to spare at 24pt).
surah_banner_glyphmap_tsv="$(mktemp)"
python ../../../tools/quran/gen_surah_banner_glyphmap.py > "$surah_banner_glyphmap_tsv"
python fontconvert.py surah_banner_24_regular 24 \
  ../builtinFonts/source/SurahNameV4/surah-name-v4.ttf \
  --2bit --compress --script none --additional-intervals 0xE010,0xE082 \
  --glyph-map "$surah_banner_glyphmap_tsv" > ../builtinFonts/surah_banner_24_regular.h
rm -f "$surah_banner_glyphmap_tsv"
echo "Generated ../builtinFonts/surah_banner_24_regular.h"

# Belajar Baca (kids learn-to-read app) display faces. Both are deliberately
# huge -- a 3-4 year old reads one glyph at arm's length -- and deliberately
# narrow in coverage, which is what keeps a face this large affordable in flash.
#
# Both pass --line-height 255 because EpdFontData::advanceY is a uint8_t: Andika's
# own line height is 322px at 96pt and Noto Naskh's 284px at 88pt, neither of which
# fits. Clamping it is safe *here* and nowhere else -- a card draws one centred
# line and never wraps, so the only consumer of the value is the activity's own
# vertical centring. Per-glyph metrics are untouched, so the letters are full size.
#
# Latin: Andika (SIL, OFL, source/Andika/) rather than the Bitter/Lexend faces
# used elsewhere. Andika is drawn specifically for early literacy: single-storey
# 'a' and 'g' matching how a child is taught to write them, and unambiguous
# I/l/1 and 0/O. Letters only (A-Z, a-z) -- digits and punctuation never appear
# on a card, and every excluded glyph is pure flash saving at this size.
python fontconvert.py kidslatin_96_regular 96 ../builtinFonts/source/Andika/Andika-Regular.ttf \
  --2bit --compress --script none --line-height 255 \
  --additional-intervals 0x41,0x5A --additional-intervals 0x61,0x7A \
  > ../builtinFonts/kidslatin_96_regular.h
echo "Generated ../builtinFonts/kidslatin_96_regular.h"

# Picture-card cut, Latin only: a card with a picture has room for more than
# 48pt but not for 96pt. No Arabic twin -- only Latin stages carry pictures,
# and KidsReadActivity keeps this cut off Arabic tracks.
python fontconvert.py kidslatin_72_regular 72 ../builtinFonts/source/Andika/Andika-Regular.ttf   --2bit --compress --script none --line-height 255   --additional-intervals 0x41,0x5A --additional-intervals 0x61,0x7A   > ../builtinFonts/kidslatin_72_regular.h
echo "Generated ../builtinFonts/kidslatin_72_regular.h"

# Arabic: Noto Naskh at 88pt (below the Latin cut -- Arabic needs headroom above
# and below the baseline for harakat, so it fills a line at a smaller point size).
# Same --reposition-marks as the reading sizes above, so fathah/kasrah/dammah sit
# at fixed heights instead of relying on GPOS the renderer doesn't implement.
# Intervals: base letters + harakat (0x0621-0x0652) and Presentation Forms-B
# (0xFE70-0xFEFC) so ArabicShaper's initial/medial/final lookups all resolve --
# a kid's card shows joined words like "kataba", not just isolated letters.
python fontconvert.py kidsarabic_88_regular 88 \
  ../builtinFonts/source/NotoNaskhArabic/NotoNaskhArabic-Regular.ttf \
  --2bit --compress --reposition-marks --script none --line-height 255 \
  --additional-intervals 0x0621,0x0652 --additional-intervals 0xFE70,0xFEFC \
  > ../builtinFonts/kidsarabic_88_regular.h
echo "Generated ../builtinFonts/kidsarabic_88_regular.h"

# Word-card cuts of the same two faces. A single letter or a two-letter syllable
# fits the panel at the sizes above; a four-letter word like "mama" does not, so the card
# measures its glyph and drops to these when the big cut would overflow. Same
# coverage, so the fallback can never hit a missing glyph.
python fontconvert.py kidslatin_48_regular 48 ../builtinFonts/source/Andika/Andika-Regular.ttf \
  --2bit --compress --script none \
  --additional-intervals 0x41,0x5A --additional-intervals 0x61,0x7A \
  > ../builtinFonts/kidslatin_48_regular.h
echo "Generated ../builtinFonts/kidslatin_48_regular.h"

python fontconvert.py kidsarabic_48_regular 48 \
  ../builtinFonts/source/NotoNaskhArabic/NotoNaskhArabic-Regular.ttf \
  --2bit --compress --reposition-marks --script none \
  --additional-intervals 0x0621,0x0652 --additional-intervals 0xFE70,0xFEFC \
  > ../builtinFonts/kidsarabic_48_regular.h
echo "Generated ../builtinFonts/kidsarabic_48_regular.h"

# Grid cuts. The introduce-the-letters screen shows a whole set at once, and a
# Latin cell now carries BOTH cases ("Aa"), which is about 150px wide at 48pt --
# two columns and seven pages for the alphabet. At 32pt the same pair fits three
# to a row and the set lands in three pages, still far above reading size.
python fontconvert.py kidslatin_32_regular 32 ../builtinFonts/source/Andika/Andika-Regular.ttf   --2bit --compress --script none   --additional-intervals 0x41,0x5A --additional-intervals 0x61,0x7A   > ../builtinFonts/kidslatin_32_regular.h
echo "Generated ../builtinFonts/kidslatin_32_regular.h"

python fontconvert.py kidsarabic_32_regular 32   ../builtinFonts/source/NotoNaskhArabic/NotoNaskhArabic-Regular.ttf   --2bit --compress --reposition-marks --script none   --additional-intervals 0x0621,0x0652 --additional-intervals 0xFE70,0xFEFC   > ../builtinFonts/kidsarabic_32_regular.h
echo "Generated ../builtinFonts/kidsarabic_32_regular.h"

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
