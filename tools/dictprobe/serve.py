"""Reader-plus-dictionary in a browser tab: click a word, see what it looks up.

Scope is deliberately narrow. This is not the app -- no e-ink rendering, no page
layout, no menus. It is the reading page and the definition screen, so the two
things that decide whether a lookup is right can be tried in a second instead of
a flash cycle: which word the reader hands to the dictionary, and which headword
comes back.

What is real: every lookup decision. The page's words go through the firmware's
own cleanWord() and fallbackForms() and are probed against the real .idx and
.syn, because the C++ in tools/dictprobe does that work.

What is simulated: everything around it. Word splitting mirrors the layout
parser (ASCII whitespace only) rather than sharing its code, and the definition
is decompressed and stripped here rather than by the firmware's DictZip and HTML
pass, so a definition that renders badly on the device can still look fine here.

    python tools/dictprobe/serve.py "<dictdir>" --epub book.epub
    python tools/dictprobe/serve.py "<dictdir>" --text page.txt --port 8000
"""

import argparse
import glob
import gzip
import html
import io
import json
import os
import re
import struct
import subprocess
import sys
import webbrowser
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from epub_words import TextCollector, spine_documents  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROBE = os.path.join(REPO, "build", "test", "dictprobe", "dictprobe.exe")
if not os.path.exists(PROBE):
    PROBE = os.path.join(REPO, "build", "test", "dictprobe", "dictprobe")

# The layout parser splits on these and nothing else, so the token a reader taps
# keeps its punctuation and diacritics (ChapterHtmlSlimParser.cpp).
# Whitespace, and the punctuation that ends a word wherever it stands: a mushaf
# writes "…﴾[الأعراف: ١٠-١٣]" with no space in it, and the word is what stands
# before the first of those. ArabicStem::CUT_TOKENS is the same list.
SPLIT = re.compile(r"([ \t\r\n]+|[\ufd3e\ufd3f\[\](){}<>|\u060c\u061b\u061f\u06d4]+)")
# A token with no letter in it is punctuation, not a word to look up.
HAS_LETTER = re.compile(r"[^\W\d_]", re.UNICODE)


# The token before the one being looked up, joined to it. Arabic settles a word's
# part of speech in its neighbour as often as in its own letters -- أَلَمۡ أَقُلۡ is
# لم + أقل, and لم makes أقل a verb -- so the same word in two places is two
# lookups, and the pair is what the cache is keyed on. The bar is one of the
# characters SPLIT already cuts on, so no token can contain it.
# The word before only ever settles an Arabic reading -- it is read for the jazm
# particle and nothing else -- so anywhere else it is noise. Keying on it would
# probe "cat" once for every place it appears on the page and get the same answer
# every time.
AR_LETTER = re.compile(r"[\u0600-\u06FF\u0750-\u077F\uFB50-\uFDFF\uFE70-\uFEFF]")


def governing(before):
    return before if before and AR_LETTER.search(before) else ""


def pair_key(before, word):
    return governing(before) + "|" + word


# The nearest thing to a plain meaning an article holds. Lane does not write
# definitions, he writes sourced statements, and a statement's rendering into
# English is the part of it a reader wants first: it opens with a subject Lane
# supplied -- He, It, They -- or with the article of a noun, and it runs to the
# full stop. What comes before it is the morphology (the variant spellings, the
# masdars, who reported them), and what comes after is the next claim.
CITE = re.compile(r"\[\d+\]")
# In order of how surely the run is a rendering rather than a remark about one.
SUBJECT = re.compile(r"\b(?:He|It|She|They|One|We|Thou)\b")
ARTICLE_WORD = re.compile(r"\b(?:A|An|The|Such|Any)\b")
CAPITALISED = re.compile(r"\b[A-Z][a-z]{2,}\b")
# A construction note, not a meaning: "With سمع [as transitive by itself]".
NOT_A_GLOSS = re.compile(r"^(?:With|See|Compare)\b")
GLOSS_MAX = 170
# A leading run of /…/ groups, however many the entry lists.
PRONUNCIATION = re.compile(r"^(?:\s*/[^/]{1,40}/\s*[,;]?)+")


def _depths(text):
    """Bracket depth at every index. Lane's editorial insertions and his
    parenthesised attributions are asides -- a rendering is never inside one."""
    out, depth = [], 0
    for ch in text:
        if ch in "[(":
            depth += 1
        out.append(depth)
        if ch in "])":
            depth = max(0, depth - 1)
    return out


def _cut(text):
    """Up to the full stop that ends the rendering. Lane writes "i. e." and
    "e. g." with stops in them, so one followed by a lower-case letter is not
    the end of anything."""
    depth = _depths(text)
    for i, ch in enumerate(text):
        if ch != "." or depth[i]:
            continue
        tail = text[i + 1:i + 3].strip()
        if tail and tail[0].islower():
            continue
        return text[:i + 1]
    return text


def _trim(text):
    text = text.strip(" \u2014-,;:\u00b7")
    if len(text) <= GLOSS_MAX:
        return text
    head = text[:GLOSS_MAX]
    stop = max(head.rfind(";"), head.rfind(","))
    return (head[:stop] if stop > GLOSS_MAX // 2 else head.rsplit(" ", 1)[0]) + " \u2026"


def house_gloss(text):
    bullets = [l.strip()[1:].strip() for l in text.split("\n") if l.strip().startswith("\u2022")]
    for bullet in bullets:
        body = CITE.sub("", bullet)
        depth = _depths(body)
        for pattern in (SUBJECT, ARTICLE_WORD, CAPITALISED):
            for m in pattern.finditer(body):
                if depth[m.start()]:
                    continue
                piece = _trim(_cut(body[m.start():]))
                if len(piece.split()) >= 3 and not NOT_A_GLOSS.match(piece):
                    return piece
                break
    # An article with no rendering in it at all -- a Sihah or Arramooz one, which
    # is Arabic throughout. Its first claim is still the nearest thing it holds.
    return _trim(_cut(CITE.sub("", bullets[0]))) if bullets else ""


def gloss(text):
    """The house format above, or whatever another dictionary happens to look like."""
    if any(line.strip().startswith("\u2022") for line in text.split("\n")):
        return house_gloss(text)
    # No bullets, no notes, no Lane. Any other dictionary puts the meaning first
    # and the discussion after it, so the opening sentence is the nearest thing to
    # a gloss it has -- and the brief showing that beats the brief showing nothing.
    #
    # Past the pronunciation, which is not one. Slashes around a phonetic run is
    # near-universal, and an entry that opens with four of them -- /haʊs/, /huːs/,
    # /hæʊs/, /hɐʊs/ -- would otherwise spend the whole gloss saying how the word
    # sounds.
    flat = PRONUNCIATION.sub("", re.sub(r"\s+", " ", text).strip())
    return _trim(_cut(flat.strip(" ,;")))


def run_probe(dictdir, pairs):
    """One dictprobe run for many words; it is the real lookup, so cache it.

    `pairs` is (word before, word) -- dictprobe reads a tab as that separator."""
    if not pairs:
        return {}
    listing = os.path.join(os.path.dirname(PROBE), "_serve_words.txt")
    with open(listing, "w", encoding="utf-8", newline="\n") as out:
        out.write("\n".join((before or "") + "\t" + word for before, word in pairs))
    result = subprocess.run(
        [PROBE, dictdir, "--json", "--words", listing],
        capture_output=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode("utf-8", "replace"))
    hits = {}
    for line in result.stdout.decode("utf-8", "replace").splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        entry = json.loads(line)
        hits[pair_key(entry.get("before"), entry["word"])] = entry
    return hits


class Definitions:
    """The .dict payload, so a hit can show the article the device would open."""

    def __init__(self, dictdir):
        self.data = b""
        plain = glob.glob(os.path.join(dictdir, "*.dict"))
        packed = glob.glob(os.path.join(dictdir, "*.dict.dz"))
        if plain:
            with open(plain[0], "rb") as handle:
                self.data = handle.read()
        elif packed:
            # dictzip is gzip with a chunk table, and gzip reads it as an
            # ordinary member; the device seeks per chunk instead, which matters
            # on 380KB of RAM and not here.
            with gzip.open(packed[0], "rb") as handle:
                self.data = handle.read()

    def text(self, offset, size):
        raw = self.data[offset : offset + size]
        body = raw.decode("utf-8", "replace")
        # The firmware strips tags and entities before laying the text out; this
        # keeps line structure so the result is readable in a browser.
        body = re.sub(r"(?i)<br\s*/?>", "\n", body)
        body = re.sub(r"(?i)</(p|div|li|h[1-6])>", "\n", body)
        body = re.sub(r"<[^>]+>", "", body)
        return html.unescape(body).strip()


def page_words(args):
    """The book text, as the lines and tokens the reader would show."""
    if args.text:
        with open(args.text, encoding="utf-8") as handle:
            return handle.read()
    if args.epub:
        with zipfile.ZipFile(args.epub) as book:
            names = set(book.namelist())
            documents = [p for p in spine_documents(book) if p in names]
            chunks = []
            for path in documents:
                collector = TextCollector()
                collector.feed(book.read(path).decode("utf-8", "replace"))
                text = "".join(collector.parts).strip()
                if len(text) <= 400:
                    continue
                if args.find:
                    # A named chapter, rather than counting past front matter
                    # that differs from one book to the next.
                    if args.find in text[:2000]:
                        return text
                    continue
                chunks.append(text)
                if len(chunks) >= args.skip + args.chapters:
                    break
            if args.find:
                raise SystemExit("no chapter contains: " + args.find)
            return "\n\n".join(chunks[args.skip : args.skip + args.chapters])
    return "الحمد لله رب العالمين\nوَيُفْسِدُ في الأرض ويسفك الدماء"


PAGE = r"""<meta charset="utf-8">
<title>Reader x dictionary</title>
<style>
  :root { --ink: #111; --paper: #fff; }
  /* display:grid on #popup and #overlay would otherwise beat the attribute. */
  [hidden] { display: none !important; }
  body { margin: 0; font: 13px/1.5 system-ui, sans-serif; background: #6b6b6b; color: #1a1a1a;
         display: grid; grid-template-columns: auto minmax(320px, 1fr); gap: 24px;
         align-items: start; padding: 20px; }

  /* ---- the device ------------------------------------------------------ */
  /* Every number inside #screen is a device pixel at scale 1; the whole screen
     is scaled as one unit so the pixel grid stays honest. */
  #rig { position: sticky; top: 20px; }
  #device { background: #2b2b2b; border-radius: 18px; padding: 18px 18px 26px;
            box-shadow: 0 10px 30px rgba(0,0,0,.35); width: max-content; }
  #screen { position: relative; width: 480px; height: 800px; background: var(--paper);
            color: var(--ink); overflow: hidden; transform-origin: top left; }
  #frame { width: 480px; height: 800px; overflow: hidden; }
  #caption { color: #ddd8cc; font-size: 12px; text-align: center; margin-top: 10px; }

  /* Reader page. The device draws one paginated screenful; this scrolls instead,
     so a whole chapter can be clicked through without a page-turn simulation. */
  #page { position: absolute; left: 8px; top: 14px; width: 464px; height: 764px;
          overflow-y: auto; scrollbar-width: none; }
  #page::-webkit-scrollbar { display: none; }
  #page p { direction: rtl; text-align: justify; margin: 0 0 18px;
            font-family: "Noto Naskh Arabic", "Traditional Arabic", serif;
            font-size: 34px; line-height: 50px; }
  .w { cursor: pointer; padding: 0 2px; border-radius: 3px; }
  .w.sel { background: var(--ink); color: var(--paper); }
  /* Not on the device: the reader gives a miss no mark at all. Kept because the
     point of this page is finding them. */
  .w.miss { box-shadow: inset 0 -2px 0 #c0392b; }

  /* Side hints: white knock-out strip, then two rotated labels. */
  #sidebg { position: absolute; left: 442px; top: 341px; width: 38px; height: 168px; background: var(--paper); }
  .sidehint { position: absolute; left: 450px; width: 30px; height: 78px; box-sizing: border-box;
              border: 1px solid var(--ink); border-radius: 0 6px 6px 0; background: var(--paper);
              display: grid; place-items: center; cursor: pointer;
              transition: transform 120ms ease-out, background 120ms ease-out, color 120ms ease-out; }
  .sidehint span { writing-mode: vertical-rl; font-size: 11px; }
  /* The bezel button is to the right of the glass, so hovering pulls the label
     out from under it and pressing pushes it back in. */
  .sidehint:hover { transform: translateX(-3px); background: var(--ink); color: var(--paper); }
  .sidehint:active { transform: translateX(1px); }
  #sideup { top: 345px; }
  #sidedown { top: 428px; }

  /* Front hints: 80x40 boxes, top corners rounded, 1px outline, bottom flush. */
  #hints { position: absolute; left: 0; top: 760px; width: 480px; height: 40px; }
  .hint { position: absolute; top: 0; width: 80px; height: 40px; box-sizing: border-box;
          border: 1px solid var(--ink); border-bottom: 0; border-radius: 6px 6px 0 0;
          background: var(--paper); font-size: 11px; text-align: center; padding-top: 7px;
          cursor: pointer;
          transition: transform 120ms ease-out, background 120ms ease-out, color 120ms ease-out; }
  .hint.stub { top: 25px; height: 15px; border-radius: 0; }
  /* Hovering lifts the tab out of the bezel; a press seats it back. Monochrome
     only -- the panel is 1-bit, so the feedback is invert, never a tint. */
  .hint:not(.stub):hover { transform: translateY(-4px); background: var(--ink); color: var(--paper); }
  .hint:not(.stub):active { transform: translateY(0); }
  .hint.stub { cursor: default; }
  @media (prefers-reduced-motion: reduce) {
    .hint, .sidehint { transition: none; }
    .hint:not(.stub):hover, .sidehint:hover { transform: none; }
  }

  /* Definition overlay. */
  #overlay { position: absolute; left: 10px; top: 190px; width: 460px; height: 600px; overflow: hidden;
             box-sizing: border-box; background: var(--paper); border: 2px solid var(--ink); }
  #title { position: absolute; left: 10px; top: 10px; font-weight: 700; font-size: 20px;
           line-height: 25px; font-family: "Noto Naskh Arabic", Inter, system-ui, sans-serif; }
  #pageind { position: absolute; right: 10px; top: 12px; font-size: 11px; line-height: 23px; }
  #decomp { position: absolute; right: 10px; left: 10px; top: 37px; height: 27px; direction: rtl;
            text-align: right; font-size: 17px; line-height: 23px;
            font-family: Tajawal, "Noto Sans Arabic", sans-serif; }
  #sep { position: absolute; left: 10px; width: 440px; height: 1px; background: var(--ink); }
  #body { position: absolute; left: 10px; width: 440px; }
  #body .row, #bgloss .row { position: absolute; left: 0; width: 440px; font-size: 19px;
               font-family: Inter, "Segoe UI", system-ui, sans-serif; white-space: pre; }
  /* One run per span, each in its own face -- the device draws a mixed line with one
     drawText call per run. The paragraph, not the line, picks the side it hangs off
     and the order the runs are laid down in. */
  #body .row span.ar, #bgloss .row span.ar {
               font-family: Tajawal, "Noto Sans Arabic", "Segoe UI", sans-serif; font-size: 20px; }
  #body .row.rtl, #bgloss .row.rtl { direction: rtl; text-align: right; }
  #body .row.ltr, #bgloss .row.ltr { direction: ltr; text-align: left; }

  /* The chooser. Three letters can be two different words and the marks do not
     always say which -- أَمۡرِي is أمر + ي and it is also the shape of أَرۡمِي -- so where
     more than one reading reached an entry, the reader picks before reading. Same
     rect as every other overlay, because the firmware computes it once. */
  #readings { position: absolute; left: 10px; top: 190px; width: 460px; height: 600px; overflow: hidden;
              box-sizing: border-box; background: var(--paper); border: 2px solid var(--ink); }
  #rdhead { position: absolute; left: 24px; top: 8px; font-size: 15px; line-height: 30px;
            font-family: Inter, "Segoe UI", system-ui, sans-serif; }
  #rdword { position: absolute; right: 24px; top: 8px; font-size: 21px; line-height: 30px;
            font-family: "Noto Naskh Arabic", serif; }
  #rdlist { position: absolute; left: 0; top: 58px; width: 456px; height: 532px; }
  /* One line: the headword where the reader's eye starts, and the cut that reached
     it at the far end. */
  .rdrow { position: absolute; left: 0; width: 456px; height: 40px; box-sizing: border-box;
           padding: 0 10px; direction: rtl; display: flex; align-items: center;
           justify-content: space-between; gap: 12px; cursor: pointer; }
  .rdrow.sel { background: var(--ink); color: var(--paper); }
  .rdrow .hw { font-family: "Noto Naskh Arabic", serif; font-size: 22px; line-height: 30px; }
  .rdrow .cut { font-family: Tajawal, "Noto Sans Arabic", sans-serif; font-size: 14px; line-height: 24px; }
  #rdind { position: absolute; right: 10px; bottom: 4px; font-size: 11px; line-height: 23px; }

  /* Layer 1. The word taken apart, the nearest meaning the article holds, and
     the other readings it could have been -- what a reader lands on before the
     article itself, which is one press away. */
  #brief { position: absolute; left: 10px; top: 190px; width: 460px; height: 600px; overflow: hidden;
           box-sizing: border-box; background: var(--paper); border: 2px solid var(--ink); }
  #btitle { position: absolute; left: 10px; top: 10px; font-weight: 700; font-size: 20px;
            line-height: 25px; font-family: "Noto Naskh Arabic", Inter, system-ui, sans-serif; }
  #bparts { position: absolute; right: 10px; left: 10px; top: 45px; direction: rtl;
            display: flex; flex-wrap: wrap; gap: 8px; }
  #bparts .seg { text-align: center; }
  /* 1-bit: the stem is boxed solid and an affix dashed. No fills, no tints -- the
     panel beside the device is where colour is allowed. */
  #bparts .box { display: block; border: 2px solid var(--ink); border-radius: 4px; padding: 0 10px;
                 font-family: "Noto Naskh Arabic", serif; font-size: 21px; line-height: 29px; }
  #bparts .affix .box { border-width: 1px; border-style: dashed; }
  #bparts .lab { display: block; font-size: 11px; line-height: 16px;
                 font-family: Tajawal, "Noto Sans Arabic", sans-serif; }
  #bsep { position: absolute; left: 10px; width: 440px; height: 1px; background: var(--ink); }
  #bgloss { position: absolute; left: 10px; width: 440px; }
  /* Under the gloss rather than pinned to the foot of the screen: it is the last
     thing the brief has to say, not a footer. Its top is set from the JS, two
     blank lines below the last line of the gloss. */
  #balt { position: absolute; left: 10px; width: 440px;
          font-family: Tajawal, "Noto Sans Arabic", sans-serif; }
  /* The label reads left to right and the readings right to left, so they get a
     line each rather than fighting over one. Both are set in the definition font
     at the definition's pitch -- the size the reader chose for results applies
     here too, because this is one. */
  #balt .altlab { direction: ltr; font-family: Inter, "Segoe UI", system-ui, sans-serif;
                  font-size: 19px; line-height: 25px; }
  #balt .altrow { direction: rtl; font-family: Tajawal, "Noto Sans Arabic", sans-serif;
                  font-size: 20px; line-height: 29px; }
  #balt .alt { font-family: Tajawal, "Noto Sans Arabic", "Segoe UI", sans-serif; font-size: 20px;
               border-bottom: 1px dotted var(--ink); cursor: pointer; margin-inline-start: 12px; }
  #balt .alt:hover, #balt .alt.sel { background: var(--ink); color: var(--paper); }
  #balt .alt.sel { border-bottom-style: solid; }

  /* "Did you mean" list: DictionarySuggestionsActivity, same rect as the
     definition overlay because the firmware computes it the same way
     (max(screenHeight * 3/4, 160), inset 10). */
  #suggest { position: absolute; left: 10px; top: 190px; width: 460px; height: 600px; overflow: hidden;
             box-sizing: border-box; background: var(--paper); border: 2px solid var(--ink); }
  #sughead { position: absolute; left: 24px; top: 8px; font-size: 15px; line-height: 30px;
             font-family: Inter, "Segoe UI", system-ui, sans-serif; }
  #sugword { position: absolute; right: 24px; top: 15px; font-size: 13px; line-height: 23px;
             font-family: "Noto Naskh Arabic", serif; }
  #suglist { position: absolute; left: 0; top: 58px; width: 456px; height: 532px; }
  /* A row is the black bar plus its text; the bar is the row, so the selection
     bleeds to both edges of the panel exactly as fillRect(rect.x, .., rect.width) does. */
  .sugrow { position: absolute; left: 0; width: 456px; height: 33px; box-sizing: border-box;
            padding: 0 10px; direction: rtl; text-align: right; font-size: 20px; line-height: 27px;
            font-family: Tajawal, "Noto Sans Arabic", sans-serif; cursor: pointer; }
  .sugrow.la { direction: ltr; text-align: left; font-size: 19px;
               font-family: Inter, "Segoe UI", system-ui, sans-serif; }
  .sugrow.sel { background: var(--ink); color: var(--paper); height: 37px; }
  #sugind { position: absolute; right: 10px; bottom: 4px; font-size: 11px; line-height: 23px; }

  /* Popup, drawn while a lookup runs. */
  #popup { position: absolute; top: 132px; height: 54px; background: var(--ink); color: var(--paper);
           border-radius: 6px; display: grid; place-items: center; font-size: 16px;
           box-shadow: 0 0 0 2px var(--paper); }

  /* ---- the panel beside it (not on the device) ------------------------- */
  #debug { background: #f6f5f2; border-radius: 10px; padding: 18px 20px; min-height: 200px; }
  #debug h2 { font-size: 11px; text-transform: uppercase; letter-spacing: .08em; color: #8a8578;
              margin: 18px 0 6px; font-weight: 600; }
  #debug h2:first-child { margin-top: 0; }
  #split { direction: rtl; display: flex; flex-wrap: wrap; gap: 6px; margin: 0 0 4px; }
  #split .seg { text-align: center; }
  #split .pill { display: block; font-family: "Noto Naskh Arabic", serif; font-size: 21px;
                 padding: 3px 12px; border-radius: 16px; }
  #split .core .pill { background: #eef7ee; color: #3f7a3b; }
  #split .affix .pill { background: #fbeede; color: #b5651d; }
  #split .lab { display: block; font-size: 10px; color: #8a8578; margin-top: 2px; }
  #split.unanswered { opacity: .55; }
  #meta { color: #6b6b6b; font-size: 12px; }
  #gloss { font-size: 13px; line-height: 1.45; }
  ol { margin: 0; padding-inline-start: 22px; }
  li { direction: rtl; text-align: right; font-family: "Noto Naskh Arabic", serif; font-size: 19px;
       padding: 1px 0; color: #6b6b6b; }
  li.hit { color: #1a1a1a; font-weight: 600; }
  li.hit::after { content: " \2190 answered"; font-family: system-ui; font-size: 11px; font-weight: 400;
                  color: #2e7d32; direction: ltr; }
  li .join { direction: rtl; font-size: 13px; color: #a09a8c; margin-inline-start: 8px; }
  li .why { direction: ltr; font-size: 11px; color: #a09a8c; margin-inline-start: 8px;
            font-family: Inter, system-ui, sans-serif; }
  #empty { color: #8a8578; font-size: 13px; }
  code { font-family: ui-monospace, monospace; font-size: 11px; }
  #zoom { color: #ddd8cc; font-size: 12px; text-align: center; margin: 0 0 10px; }
  #zoom select { font-size: 12px; }
</style>

<div id="rig">
  <div id="zoom">
    scale <select id="scalesel">
      <option value="1">1x (480 x 800, actual pixels)</option>
      <option value="0.8">0.8x</option>
      <option value="1.25">1.25x</option>
    </select>
  </div>
  <div id="device">
    <div id="frame"><div id="screen">
      <div id="page">__BODY__</div>
      <div id="sidebg"></div>
      <div class="sidehint" id="sideup"><span>Up</span></div>
      <div class="sidehint" id="sidedown"><span>Down</span></div>
      <div id="overlay" hidden>
        <div id="title"></div>
        <div id="pageind"></div>
        <div id="decomp"></div>
        <div id="sep"></div>
        <div id="body"></div>
      </div>
      <div id="readings" hidden>
        <div id="rdhead">Which reading</div>
        <div id="rdword"></div>
        <div id="rdlist"></div>
        <div id="rdind"></div>
      </div>
      <div id="brief" hidden>
        <div id="btitle"></div>
        <div id="bparts"></div>
        <div id="bsep"></div>
        <div id="bgloss"></div>
        <div id="balt"></div>
      </div>
      <div id="suggest" hidden>
        <div id="sughead">Did you mean</div>
        <div id="sugword"></div>
        <div id="suglist"></div>
        <div id="sugind"></div>
      </div>
      <div id="popup" hidden>Looking up word</div>
      <div id="hints"></div>
    </div></div>
    <div id="caption">X4 &middot; 480 x 800 &middot; 1-bit</div>
  </div>
</div>

<div id="debug">
  <div id="empty">Click a word on the screen. The device shows the definition; this column shows why.</div>
  <div id="card" hidden>
    <h2>How the word was read</h2>
    <div id="split"></div>
    <div id="meta"></div>
    <h2>Nearest meaning in the article</h2>
    <div id="gloss"></div>
    <h2>Candidates tried, in order</h2>
    <ol id="forms"></ol>
  </div>
</div>

<script>
const DATA = __DATA__;

// ---- device metrics, copied from the firmware ---------------------------
const M = {
  overlayX: 10, overlayY: 190, overlayW: 460, overlayH: 600,
  padding: 10,
  titleLine: 25,        // getLineHeight(UI_10_FONT_ID)
  smallLine: 23,        // getLineHeight(SMALL_FONT_ID)
  bodyPitch: 25,        // getLineHeight(UI_10_FONT_ID), the default definition font
  arabicPitch: 29,      // max(25, Tajawal 10 = 25) + 25/6
  blankPitch: 12,       // pitch / 2
  bodyW: 440,
  // DictionarySuggestionsActivity: listTop = rect.y + 58, listHeight = rect.height - 68,
  // rowHeight = getLineHeight(definition font) + 8, and the selected row grows by
  // the Arabic descender pad so the tails are not sliced.
  sugRow: 33, sugListH: 532, sugDescender: 4,
  // Air above a paragraph, so a list of senses reads as a list. The article is a
  // chain of separate claims, and they are only separate on the page if something
  // separates them; a blank line would cost a whole line's height per bullet on a
  // screen that holds twenty of them.
  paraLead: 10,
  // Two blank lines, which is what the reader asked of both the readings line at
  // the foot of the brief and the sources at the foot of an article: enough that
  // the eye stops before it, without a rule drawn across the screen.
  blockLead: 50,
};
const HINTS = [58, 146, 254, 342];

let selected = null;
let state = 'select';   // 'select' | 'suggest' | 'readings' | 'brief' | 'definition'
let current = null;     // the clicked word's probe data
// Which reading the two screens are showing. The probe answers with one, and the
// brief screen can be handed another off the "also read as" line without a second
// lookup -- the candidate already carries where its article lives.
let reading = null;
let page = 0, pages = [];
let cameFromSuggestions = false;   // decides where Back goes, as the device's result callback does
let readings = [], rdIndex = 0, cameFromReadings = false;
// Which of the other readings the brief has highlighted, -1 for none. The screen
// opens with none: the reading it is showing is the one that answered, and the
// line underneath is there to be disbelieved, not to be stepped through first.
let altIndex = -1, altRows = [];

// ---- the firmware's own wrap, in JS -------------------------------------
// DictionaryDefinitionActivity::wrapText: one wrapped line never mixes scripts,
// because drawText sends a whole line down the Arabic path as soon as it holds
// one Arabic codepoint.
const measure = (() => {
  const ctx = document.createElement('canvas').getContext('2d');
  // Read the font off a real row rather than repeating the CSS here: whichever
  // face the browser actually resolved is the one the rows are drawn in, and a
  // guess that misses makes the wrap disagree with what is on screen.
  const probe = document.createElement('div');
  probe.style.cssText = 'position:absolute;visibility:hidden;left:-9999px';
  const fonts = {};
  return (text, arabic) => {
    const key = arabic ? 'ar' : 'la';
    if (!fonts[key]) {
      probe.className = 'row' + (arabic ? ' ar' : '');
      document.getElementById('body').appendChild(probe);
      fonts[key] = getComputedStyle(probe).font;
      probe.remove();
    }
    ctx.font = fonts[key];
    return ctx.measureText(text).width;
  };
})();
const isArabic = s => /[؀-ۿݐ-ݿﭐ-﷿ﹰ-﻿]/.test(s);
// Only a letter carries a direction: the separators the dictionary uses between
// principal parts ( · ), dashes and curly quotes stay neutral and join the run
// they sit in, exactly as DictionaryDefinitionActivity::tokenScript does.
const LATIN_LETTER = /[A-Za-zÀ-ɏͰ-ϿЀ-ӿ]/;
const tokenScript = t =>
  isArabic(t) ? 'ar'
  : (LATIN_LETTER.test(t.replace(/[×÷]/g, '')) ? 'la' : 'neutral');

// One line -> the single-script pieces the device draws it from. The renderer sends
// a whole string to the Arabic face the moment it holds one Arabic letter, so a
// mixed line is drawn run by run; the wrap has to measure it the same way.
function splitRuns(line) {
  const runs = [];
  let script = 'neutral';
  const re = /( *)([^ ]+)/g;
  let m;
  while ((m = re.exec(line)) !== null) {
    const s = tokenScript(m[2]);
    const breaks = runs.length && s !== 'neutral' && script !== 'neutral' && s !== script;
    if (!runs.length || breaks) {
      runs.push({text: runs.length ? m[2] : line.slice(0, m.index) + m[1] + m[2],
                 spaces: runs.length ? m[1].length : 0, ar: s === 'ar'});
      script = s;
      continue;
    }
    runs[runs.length - 1].text += m[1] + m[2];
    if (s !== 'neutral') { script = s; runs[runs.length - 1].ar = runs[runs.length - 1].ar || s === 'ar'; }
  }
  return runs;
}

// Each run measured in the face it is drawn from: measuring a mixed string in one
// call puts all of it through the Arabic metrics and the wrap fills lines against a
// width no line actually has.
function measureLine(line) {
  const space = measure(' ', false);
  return splitRuns(line).reduce((w, r) => w + r.spaces * space + measure(r.text, r.ar), 0);
}

const MAX_DEFINITION_BYTES = 16384;   // readDefinition's read cap
const MAX_WRAPPED_LINES = 384;        // MAX_WRAPPED_DEFINITION_LINES

// "• text" -> "•  text", and a wrapped sense hangs under its own text rather than
// running back under the marker. DictionaryDefinitionActivity::openBulletGap and
// continuationPrefixFor.
const openBulletGap = line => line.replace(/^( *)\u2022 (?! )/, '$1\u2022  ');
const bulletIndent = () =>
  ' '.repeat(Math.max(3, Math.ceil(measure('\u2022  ', false) / measure(' ', false))));

function wrapDefinition(text) {
  const out = [];
  let truncated = false;
  if (new TextEncoder().encode(text || '').length > MAX_DEFINITION_BYTES) {
    const bytes = new TextEncoder().encode(text).slice(0, MAX_DEFINITION_BYTES);
    text = new TextDecoder('utf-8', {fatal: false}).decode(bytes);
    truncated = true;
  }
  for (const source of (text || '').split('\n')) {
    const line = openBulletGap(source.replace(/\t/g, '  ').replace(/\s+$/, ''));
    // Unicode decides a paragraph's side by its first letter that has one (UBA
    // P2/P3), and every line the paragraph wraps to keeps that side. Without it a
    // bullet of English quoting Arabic alternates edges as it wraps.
    let rtl = false;
    for (const token of line.split(/ +/)) {
      const s = tokenScript(token);
      if (s !== 'neutral') { rtl = s === 'ar'; break; }
    }
    // Only the FIRST line of a paragraph opens the gap: the lines it wraps to are
    // the same claim, and air inside one would break it up rather than set it off.
    // Nor above one the source already separated with a blank line -- that gap is
    // there to mark a section, and doubling it would make the section look like a
    // page break.
    let first = out.length > 0 && !!out[out.length - 1].text;
    // The sources are a different kind of thing from the senses above them --
    // they are who said it, not what it means -- so the gap that sets them apart
    // is the deliberate one rather than the ordinary paragraph's.
    const heading = line.trim() === 'Notes' && out.length > 0;
    // Measured from the last line with something on it: the source already put a
    // blank line before its Notes, and counting that twice makes the gap wider
    // than the two lines it is meant to be.
    const blankAbove = out.length > 0 && !out[out.length - 1].text;
    const push = t => {
      out.push({text: t, rtl: rtl,
                lead: heading ? (blankAbove ? M.blockLead - M.blankPitch : M.blockLead)
                              : (first ? M.paraLead : 0)});
      first = false;
    };
    if (!line) { out.push({text: '', rtl: false}); continue; }
    const indent = /^ */.exec(line)[0];
    const continuation = indent + (/^ *\u2022/.test(line) ? bulletIndent() : '');
    let cur = indent, has = false;
    const tokens = /( *)([^ ]+)/g;
    let m;
    while ((m = tokens.exec(line)) !== null) {
      // A gap the dictionary widened stays wide: it is what sets the bullet apart.
      const separator = has ? (m[1].length > 1 ? '  ' : ' ') : '';
      const test = cur + separator + m[2];
      if (measureLine(test) <= M.bodyW || !has) {
        cur = test; has = true;
      } else {
        push(cur.replace(/ +$/, '')); cur = continuation + m[2]; has = true;
      }
    }
    if (has) push(cur.replace(/ +$/, ''));
    if (out.length >= MAX_WRAPPED_LINES) { truncated = true; break; }
  }
  if (truncated) {
    out.length = Math.min(out.length, MAX_WRAPPED_LINES - 1);
    out.push({text: '[Truncated]', rtl: false});
  }
  return out;
}

function pitchOf(line) {
  if (!line.text) return M.blankPitch;
  return isArabic(line.text) ? M.arabicPitch : M.bodyPitch;
}

// The gap above a line, which is not part of it: a page that starts on a bullet
// drops it rather than opening the page with blank space.
const leadOf = line => line.lead || 0;

// DictionaryDefinitionActivity::paginate
function paginate(lines, bodyHeight) {
  const starts = [0];
  let used = 0;
  lines.forEach((line, i) => {
    const h = (used > 0 ? leadOf(line) : 0) + pitchOf(line);
    if (used > 0 && used + h > bodyHeight) { starts.push(i); used = 0; used += pitchOf(line); return; }
    used += h;
  });
  return starts;
}

// ---- drawing ------------------------------------------------------------
// The four boxes are built once and relabelled. Rebuilding them would detach the
// element a click is still travelling through, which swallows the second press.
function setHints(labels) {
  const box = document.getElementById('hints');
  if (!box.children.length) {
    labels.forEach((_, i) => {
      const el = document.createElement('div');
      el.className = 'hint';
      el.style.left = HINTS[i] + 'px';
      el.dataset.i = i;
      box.appendChild(el);
    });
  }
  labels.forEach((label, i) => {
    const el = box.children[i];
    el.className = 'hint' + (label ? '' : ' stub');
    el.textContent = label || '';
  });
}

function showSelectState() {
  state = 'select';
  document.getElementById('overlay').hidden = true;
  document.getElementById('brief').hidden = true;
  document.getElementById('readings').hidden = true;
  document.getElementById('suggest').hidden = true;
  document.getElementById('sidebg').hidden = false;
  document.getElementById('sideup').hidden = false;
  document.getElementById('sidedown').hidden = false;
  setHints(['« Back', 'Select', 'Left', 'Right']);
}

// ---- "Did you mean" -----------------------------------------------------
// The firmware opens this screen instead of the definition whenever the lookup
// missed but suggest() returned candidates, and it enters with item 0 already
// selected -- there is no unselected state to fall into.
let suggestions = [], sugIndex = 0, sugFrom = null;

function showSuggestions(word, list) {
  state = 'suggest';
  suggestions = list;
  sugIndex = 0;
  sugFrom = word;
  document.getElementById('overlay').hidden = true;
  document.getElementById('brief').hidden = true;
  document.getElementById('readings').hidden = true;
  document.getElementById('suggest').hidden = false;
  document.getElementById('sidebg').hidden = true;
  document.getElementById('sideup').hidden = true;
  document.getElementById('sidedown').hidden = true;
  // mapLabels() puts all four on the front row, so this screen has no side hints.
  setHints(['« Back', 'Select', 'Up', 'Down']);
  document.getElementById('sugword').textContent = word;
  renderSuggestions();
}

function renderSuggestions() {
  const list = document.getElementById('suglist');
  const perPage = Math.max(1, Math.floor(M.sugListH / M.sugRow));
  const start = Math.floor(sugIndex / perPage) * perPage;
  const end = Math.min(suggestions.length, start + perPage);
  list.innerHTML = '';
  for (let i = start; i < end; i++) {
    const row = document.createElement('div');
    row.className = 'sugrow' + (isArabic(suggestions[i]) ? '' : ' la') + (i === sugIndex ? ' sel' : '');
    row.style.top = ((i - start) * M.sugRow - 2) + 'px';
    row.textContent = suggestions[i];
    row.dataset.i = i;
    list.appendChild(row);
  }
  const pages = Math.ceil(suggestions.length / perPage);
  document.getElementById('sugind').textContent =
    suggestions.length > perPage ? (Math.floor(sugIndex / perPage) + 1) + '/' + pages : '';
}

// Select on a suggestion is a real lookup: the word is not one of the page's, so
// the server runs dictprobe for it the way the device would run its own.
async function openSuggestion() {
  const word = suggestions[sugIndex];
  if (!word) return;
  const popup = document.getElementById('popup');
  popup.hidden = false;
  let entry = null;
  try {
    entry = await fetch('/lookup?w=' + encodeURIComponent(word)).then(r => r.json());
  } catch (err) { entry = null; }
  popup.hidden = true;
  if (!entry || !entry.found) return;              // the device keeps the list up
  document.getElementById('suggest').hidden = true;
  current = entry;
  reading = entry;
  page = 0;
  cameFromSuggestions = true;
  readings = collectReadings(entry);
  rdIndex = 0;
  cameFromReadings = false;
  renderPanel(word, entry);
  renderBrief();
}

// One page of wrapped lines into an element, each row in the faces the device
// would draw it from. Shared by the two screens: the brief shows the gloss with
// the same wrap the article gets, so the line breaks are the device's own.
function drawLines(el, lines, begin, end) {
  el.innerHTML = '';
  let y = 0;
  for (let i = begin; i < end; i++) {
    if (i > begin) y += leadOf(lines[i]);
    const row = document.createElement('div');
    row.className = 'row' + (lines[i].rtl ? ' rtl' : ' ltr');
    row.style.top = y + 'px';
    row.style.height = pitchOf(lines[i]) + 'px';
    splitRuns(lines[i].text).forEach(r => {
      const span = document.createElement('span');
      span.className = r.ar ? 'ar' : 'la';
      span.textContent = ' '.repeat(r.spaces) + r.text;
      row.appendChild(span);
    });
    el.appendChild(row);
    y += pitchOf(lines[i]);
  }
}

// ---- the chooser --------------------------------------------------------
// Every reading of the word that reached an entry of its own, in the order the
// index was asked for them -- so the one that answered is first, and the rest are
// what it beat. Each carries how it was arrived at, which is the only thing
// resembling a confidence the stemmer can honestly report.
function collectReadings(d) {
  const out = [], seen = {};
  const push = (headword, split, standing, entry) => {
    if (!headword || seen[headword]) return;
    seen[headword] = true;
    out.push({headword: headword, split: split, standing: standing, entry: entry});
  };
  // An exact hit cut nothing off, so no candidate carries it.
  if (d.found && d.source && d.source.indexOf('exact') === 0) {
    push(d.headword, d.split, 'the word as written', null);
  }
  (d.forms || []).forEach(f => {
    if (f.entry) push(f.entry.headword, f, f.standing, f.entry);
  });
  return out;
}

function showReadings() {
  state = 'readings';
  document.getElementById('overlay').hidden = true;
  document.getElementById('brief').hidden = true;
  document.getElementById('suggest').hidden = true;
  document.getElementById('readings').hidden = false;
  document.getElementById('sidebg').hidden = true;
  document.getElementById('sideup').hidden = true;
  document.getElementById('sidedown').hidden = true;
  setHints(['\u00ab Back', 'Select', 'Up', 'Down']);
  document.getElementById('rdword').textContent = current.query || '';
  renderReadings();
}

function renderReadings() {
  const list = document.getElementById('rdlist');
  const perPage = Math.max(1, Math.floor(532 / 40));
  const start = Math.floor(rdIndex / perPage) * perPage;
  const end = Math.min(readings.length, start + perPage);
  list.innerHTML = '';
  for (let i = start; i < end; i++) {
    const row = document.createElement('div');
    row.className = 'rdrow' + (i === rdIndex ? ' sel' : '');
    row.style.top = ((i - start) * 40) + 'px';
    row.dataset.i = i;
    const hw = document.createElement('span');
    hw.className = 'hw'; hw.textContent = readings[i].headword;
    const cut = document.createElement('span');
    cut.className = 'cut';
    cut.textContent = (readings[i].split && readings[i].split.join) || '';
    row.appendChild(hw); row.appendChild(cut);
    list.appendChild(row);
  }
  const pages = Math.ceil(readings.length / perPage);
  document.getElementById('rdind').textContent =
    readings.length > perPage ? (Math.floor(rdIndex / perPage) + 1) + '/' + pages : '';
}

// Opening one is a read, not a lookup: the candidate already carries where its
// article lives. The reading that answered has its text loaded already.
async function openReading(which) {
  const pick = readings[which];
  if (!pick) return;
  if (!pick.entry || pick.headword === current.headword) {
    reading = current;
    cameFromReadings = readings.length > 1;
    renderBrief();
    return;
  }
  const popup = document.getElementById('popup');
  popup.hidden = false;
  let article = null;
  try {
    article = await fetch('/entry?o=' + pick.entry.offset + '&s=' + pick.entry.size).then(r => r.json());
  } catch (err) { article = null; }
  popup.hidden = true;
  if (!article) return;
  reading = {found: true, headword: pick.headword, query: current.query,
             definition: article.definition, gloss: article.gloss, split: pick.split};
  page = 0;
  cameFromReadings = readings.length > 1;
  renderBrief();
}

// ---- layer 1: the brief -------------------------------------------------
// What the word was read as, what it most nearly means, and what else it could
// have been. A lookup lands here; the article is one press away.
function renderBrief() {
  state = 'brief';
  const d = reading || current;
  document.getElementById('overlay').hidden = true;
  document.getElementById('suggest').hidden = true;
  document.getElementById('readings').hidden = true;
  document.getElementById('brief').hidden = false;
  document.getElementById('sidebg').hidden = false;

  document.getElementById('btitle').textContent = d.found ? d.headword : d.query;

  // Every part under the name of what it is -- the same segmentation the panel
  // draws, in the only two weights a 1-bit screen has.
  const parts = document.getElementById('bparts');
  parts.innerHTML = '';
  // One part with nothing to say about it is the headword written twice: an
  // English word cut nowhere and named nothing. The row goes away rather than
  // repeating the title back at the reader.
  const shown = (d.split && d.split.parts) || [];
  const worthShowing = shown.length > 1 || (shown.length === 1 && shown[0].label);
  (worthShowing ? shown : []).forEach(p => {
    const seg = document.createElement('div');
    seg.className = 'seg ' + p.kind;
    const box = document.createElement('span');
    box.className = 'box'; box.textContent = p.text;
    const lab = document.createElement('span');
    lab.className = 'lab'; lab.textContent = p.label;
    seg.appendChild(box); seg.appendChild(lab);
    parts.appendChild(seg);
  });

  // The other readings that reached an entry of their own. Ordering is a ranking,
  // not a verdict, and this is where a reader who does not believe it looks --
  // and, with the bezel's Up and Down, acts on it.
  const alt = document.getElementById('balt');
  alt.innerHTML = '';
  const altRow = document.createElement('div');
  altRow.className = 'altrow';
  const seen = {};
  seen[d.headword] = true;
  ((current && current.forms) || []).forEach((f, i) => {
    if (!f.entry || seen[f.entry.headword]) return;
    seen[f.entry.headword] = true;
    if (altRow.children.length) altRow.appendChild(document.createTextNode(' · '));
    const el = document.createElement('span');
    el.className = 'alt';
    el.textContent = f.entry.headword;
    el.dataset.i = i;
    altRow.appendChild(el);
  });
  altRows = [...altRow.querySelectorAll('.alt')];
  if (altIndex >= altRows.length) altIndex = altRows.length - 1;
  altRows.forEach((el, i) => el.classList.toggle('sel', i === altIndex));
  if (altRows.length) {
    const label = document.createElement('div');
    label.className = 'altlab';
    label.textContent = 'See also:';
    alt.appendChild(label);
    alt.appendChild(altRow);
  }

  // The bezel's Up and Down step through that line; the front row says what
  // pressing Select would do, which is open the highlighted reading when there is
  // one and the article when there is not.
  const navigable = altRows.length > 0;
  document.getElementById('sidebg').hidden = !navigable;
  document.getElementById('sideup').hidden = !navigable;
  document.getElementById('sidedown').hidden = !navigable;
  setHints([cameFromReadings ? '\u00ab Readings' : '\u00ab Back',
            altIndex >= 0 ? 'Open' : 'Read more', '', '']);

  // offsetHeight, not the rect: the screen is CSS-scaled and the device's own
  // layout is in unscaled pixels.
  const sepY = 45 + parts.offsetHeight + 10;
  document.getElementById('bsep').style.top = sepY + 'px';
  const glossEl = document.getElementById('bgloss');
  glossEl.style.top = (sepY + 10) + 'px';
  const text = d.found ? (d.gloss || '(no rendering in this article)') : 'Word not found';
  const lines = wrapDefinition(text);
  drawLines(glossEl, lines, 0, lines.length);

  // Two blank lines under the last line of the gloss, wherever that fell.
  const glossHeight = lines.reduce((h, line) => h + leadOf(line) + pitchOf(line), 0);
  alt.style.top = (sepY + 10 + glossHeight + M.blockLead) + 'px';
}

function renderDefinition() {
  // Set here rather than at the call sites: the brief and the suggestion list
  // both open this screen, and one of them forgetting is a Back button that goes
  // to the wrong place.
  state = 'definition';
  const d = reading || current;
  const overlay = document.getElementById('overlay');
  overlay.hidden = false;
  document.getElementById('brief').hidden = true;
  document.getElementById('readings').hidden = true;
  document.getElementById('suggest').hidden = true;
  // The overlay covers them, and the definition screen has no Up/Down.
  document.getElementById('sidebg').hidden = true;
  document.getElementById('sideup').hidden = true;
  document.getElementById('sidedown').hidden = true;
  setHints(['« Back', 'Done', 'Left', 'Right']);

  const found = d && d.found;
  document.getElementById('title').textContent = found ? d.headword : (d ? d.query : '');
  const decomp = document.getElementById('decomp');
  // Only a split that actually cut something is shown, exactly as the device
  // decides it: DictionaryStore fills decomposition only when count > 1.
  const join = (found && d.split && d.split.parts && d.split.parts.length > 1) ? d.split.join : '';
  decomp.textContent = join;
  decomp.hidden = !join;
  const decompHeight = join ? 27 : 0;

  const sepY = M.padding + M.titleLine + 12 + decompHeight;
  document.getElementById('sep').style.top = sepY + 'px';
  const bodyY = sepY + 10;
  const bodyEl = document.getElementById('body');
  bodyEl.style.top = bodyY + 'px';

  const headerHeight = M.titleLine + 20 + decompHeight;
  const footerHeight = M.smallLine + 8;
  const bodyHeight = Math.max(1, M.overlayH - headerHeight - footerHeight - M.padding);

  const text = found ? (d.definition || '(no definition text)') : 'Word not found';
  const lines = wrapDefinition(text);
  const starts = paginate(lines, bodyHeight);
  pages = starts;
  page = Math.min(page, starts.length - 1);

  const begin = starts[page];
  const end = page + 1 < starts.length ? starts[page + 1] : lines.length;
  drawLines(bodyEl, lines, begin, end);
  document.getElementById('pageind').textContent =
    starts.length > 1 ? (page + 1) + '/' + starts.length : '';
}

function renderPanel(word, entry) {
  const d = entry;
  document.getElementById('empty').hidden = true;
  document.getElementById('card').hidden = false;
  const split = document.getElementById('split');
  const forms = document.getElementById('forms');
  split.innerHTML = ''; forms.innerHTML = '';
  if (!d) { document.getElementById('meta').textContent = word + ': not probed'; return; }
  // Parts come from the firmware's own ArabicStem::segments, through dictprobe.
  ((d.split && d.split.parts) || []).forEach(p => {
    const seg = document.createElement('span');
    seg.className = 'seg ' + p.kind;
    const pill = document.createElement('span');
    pill.className = 'pill'; pill.textContent = p.text;
    const lab = document.createElement('span');
    lab.className = 'lab'; lab.textContent = p.label;
    seg.appendChild(pill); seg.appendChild(lab);
    split.appendChild(seg);
  });
  split.classList.toggle('unanswered', !d.found);
  document.getElementById('gloss').textContent = d.gloss || (d.found ? '(none found in the article)' : '');
  // What the word before settled, when it settled anything: the reading below is
  // not one the word itself could have picked.
  const governed = d.governs === 'jazm' ? ' &middot; مجزوم after <b>' + d.before + '</b>' : '';
  document.getElementById('meta').innerHTML = (d.found
    ? 'matched via <code>' + d.source + '</code> &middot; word as written: ' + d.query
    : 'not found &middot; word as written: ' + d.query) + governed;
  const probed = [{form: d.query, join: d.query}].concat(d.forms || []);
  probed.forEach((f, i) => {
    const li = document.createElement('li');
    li.textContent = f.form;
    // Every split tried, not just the answering one: a miss is either a wrong cut
    // here or a dictionary without the stem, and this says which.
    if (f.join && f.join !== f.form) {
      const join = document.createElement('span');
      join.className = 'join'; join.textContent = f.join;
      li.appendChild(join);
    }
    // How the candidate was arrived at. It belongs here and not on the device:
    // the reader picks a reading, the developer asks why it was offered.
    if (f.standing) {
      const why = document.createElement('span');
      why.className = 'why'; why.textContent = f.standing;
      li.appendChild(why);
    }
    const idx = d.found ? (d.source.startsWith('exact') ? 0 : d.formIndex + 1) : -1;
    if (i === idx) li.className = 'hit';
    forms.appendChild(li);
  });
}

function open(word, el, key) {
  if (selected) selected.classList.remove('sel');
  selected = el; el.classList.add('sel');
  current = DATA[key] || DATA['|' + word];
  reading = current;
  page = 0;
  altIndex = -1;
  cameFromSuggestions = false;
  renderPanel(word, current);
  // The device paints "Looking up word" while it probes; a beat of it here keeps
  // the state visible rather than pretending the read is free.
  const popup = document.getElementById('popup');
  popup.hidden = false;
  popup.style.left = '50%';
  popup.style.transform = 'translateX(-50%)';
  popup.style.padding = '0 16px';
  setTimeout(() => {
    popup.hidden = true;
    const d = current;
    if (d && !d.found && d.suggestions && d.suggestions.length) {
      showSuggestions(d.query || word, d.suggestions);
      return;
    }
    readings = collectReadings(current || {});
    rdIndex = 0;
    cameFromReadings = false;
    // One reading is not a choice. Straight to the brief, as before.
    if (readings.length > 1) { showReadings(); return; }
    renderBrief();
  }, 180);
}

document.querySelectorAll('.w').forEach(el => {
  el.addEventListener('click', () => open(el.dataset.w, el, el.dataset.k));
});

document.getElementById('hints').addEventListener('click', ev => {
  const hint = ev.target.closest('.hint');
  if (!hint) return;
  const which = Number(hint.dataset.i);
  if (state === 'suggest') {
    if (which === 0) { showSelectState(); return; }
    if (which === 1) { openSuggestion(); return; }
    if (which === 2 || which === 3) {
      // ButtonNavigator wraps at both ends.
      const n = suggestions.length;
      sugIndex = (sugIndex + (which === 2 ? -1 : 1) + n) % n;
      renderSuggestions();
    }
    return;
  }
  if (state === 'readings') {
    if (which === 0 && cameFromSuggestions) { showSuggestions(sugFrom, suggestions); return; }
    if (which === 0) { showSelectState(); return; }
    if (which === 1) { openReading(rdIndex); return; }
    if (which === 2 || which === 3) {
      const n = readings.length;
      rdIndex = (rdIndex + (which === 2 ? -1 : 1) + n) % n;
      renderReadings();
    }
    return;
  }
  if (state === 'brief') {
    // Back goes to the choice it came from, then to the list it came from, then out.
    if (which === 0 && cameFromReadings) { showReadings(); return; }
    if (which === 0 && cameFromSuggestions) { showSuggestions(sugFrom, suggestions); return; }
    if (which === 0) { showSelectState(); return; }
    if (which === 1) {
      if (openHighlightedAlt()) return;
      page = 0;
      renderDefinition();
    }
    return;
  }
  if (state !== 'definition') return;
  // Back returns to the brief it was opened from; Done ends the whole lookup.
  if (which === 0) { renderBrief(); return; }
  if (which === 1) { showSelectState(); return; }
  if (which === 2 && page > 0) { page--; renderDefinition(); }
  if (which === 3 && page + 1 < pages.length) { page++; renderDefinition(); }
});

// Another reading, opened without a second lookup: the candidate already carries
// where its article lives, so this is a read.
document.getElementById('balt').addEventListener('click', ev => {
  const el = ev.target.closest('.alt');
  if (!el || !current || !current.forms) return;
  const form = current.forms[Number(el.dataset.i)];
  if (!form || !form.entry) return;
  // The same readings the chooser lists, reachable without going back to it.
  const which = readings.findIndex(r => r.headword === form.entry.headword);
  if (which >= 0) { rdIndex = which; openReading(which); return; }
});

// The bezel. Nothing else on any screen uses these two, so they belong to the
// brief's readings line while it has one.
function moveAlt(step) {
  if (state !== 'brief' || !altRows.length) return;
  // -1 is a real position -- nothing highlighted, Select opens the article -- but
  // it is a position the line is left from, never one a press is spent on: from
  // rest either button lands on the line at once, Down at its first reading and
  // Up at its last. Walking off either end puts the highlight away again.
  if (altIndex < 0) altIndex = step > 0 ? 0 : altRows.length - 1;
  else {
    const next = altIndex + step;
    altIndex = (next < 0 || next >= altRows.length) ? -1 : next;
  }
  altRows.forEach((el, i) => el.classList.toggle('sel', i === altIndex));
  setHints([cameFromReadings ? '\u00ab Readings' : '\u00ab Back',
            altIndex >= 0 ? 'Open' : 'Read more', '', '']);
}

function openHighlightedAlt() {
  const el = altRows[altIndex];
  if (!el) return false;
  const form = current.forms[Number(el.dataset.i)];
  if (!form || !form.entry) return false;
  const which = readings.findIndex(r => r.headword === form.entry.headword);
  if (which < 0) return false;
  altIndex = -1;
  rdIndex = which;
  openReading(which);
  return true;
}

document.getElementById('sideup').addEventListener('click', () => moveAlt(-1));
document.getElementById('sidedown').addEventListener('click', () => moveAlt(1));

document.getElementById('rdlist').addEventListener('click', ev => {
  const row = ev.target.closest('.rdrow');
  if (!row) return;
  const i = Number(row.dataset.i);
  if (i === rdIndex) { openReading(i); return; }
  rdIndex = i;
  renderReadings();
});

document.getElementById('suglist').addEventListener('click', ev => {
  const row = ev.target.closest('.sugrow');
  if (!row) return;
  const i = Number(row.dataset.i);
  if (i === sugIndex) { openSuggestion(); return; }
  sugIndex = i;
  renderSuggestions();
});

document.addEventListener('keydown', ev => {
  if (state === 'suggest') {
    const n = suggestions.length;
    if (ev.key === 'ArrowUp') { sugIndex = (sugIndex - 1 + n) % n; renderSuggestions(); }
    if (ev.key === 'ArrowDown') { sugIndex = (sugIndex + 1) % n; renderSuggestions(); }
    if (ev.key === 'Enter') openSuggestion();
    if (ev.key === 'Escape') showSelectState();
    return;
  }
  if (state === 'readings') {
    const n = readings.length;
    if (ev.key === 'ArrowUp') { rdIndex = (rdIndex - 1 + n) % n; renderReadings(); }
    if (ev.key === 'ArrowDown') { rdIndex = (rdIndex + 1) % n; renderReadings(); }
    if (ev.key === 'Enter') openReading(rdIndex);
    if (ev.key === 'Escape') showSelectState();
    return;
  }
  if (state === 'brief') {
    if (ev.key === 'ArrowDown') { moveAlt(1); return; }
    if (ev.key === 'ArrowUp') { moveAlt(-1); return; }
    if (ev.key === 'Enter' && openHighlightedAlt()) return;
    if (ev.key === 'Enter' || ev.key === 'ArrowRight') { page = 0; renderDefinition(); }
    if (ev.key === 'Escape') {
      if (cameFromReadings) showReadings();
      else if (cameFromSuggestions) showSuggestions(sugFrom, suggestions);
      else showSelectState();
    }
    return;
  }
  if (state !== 'definition') return;
  if (ev.key === 'ArrowLeft' && page > 0) { page--; renderDefinition(); }
  if (ev.key === 'ArrowRight' && page + 1 < pages.length) { page++; renderDefinition(); }
  if (ev.key === 'Escape') renderBrief();
});

document.getElementById('scalesel').addEventListener('change', ev => {
  const s = parseFloat(ev.target.value);
  const screen = document.getElementById('screen');
  screen.style.transform = 'scale(' + s + ')';
  document.getElementById('frame').style.width = (480 * s) + 'px';
  document.getElementById('frame').style.height = (800 * s) + 'px';
});

showSelectState();
</script>
"""


# (word before, word) for every word on the page. A paragraph starts with nothing
# in front of it, and punctuation between two words breaks the pair -- no particle
# governs across a full stop. build_page walks the text the same way, so the key
# it writes onto a span is one this list already had probed.
def page_pairs(text):
    pairs = []
    for block in text.split("\n"):
        before = ""
        for token in SPLIT.split(block):
            if not token or token.isspace():
                continue
            if not HAS_LETTER.search(token):
                before = ""
                continue
            pairs.append((governing(before), token))
            before = token
    return pairs


def build_page(text, hits):
    paragraphs = []
    for block in text.split("\n"):
        if not block.strip():
            continue
        parts = []
        before = ""
        for token in SPLIT.split(block):
            if not token:
                continue
            if token.isspace():
                parts.append(" ")
                continue
            key = pair_key(before, token)
            entry = hits.get(key)
            classes = "w" + ("" if entry and entry.get("found") else " miss")
            parts.append(
                '<span class="%s" data-w="%s" data-k="%s">%s</span>'
                % (classes, html.escape(token, quote=True), html.escape(key, quote=True), html.escape(token))
            )
            before = token if HAS_LETTER.search(token) else ""
        paragraphs.append("<p>" + "".join(parts) + "</p>")
    return "".join(paragraphs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dictdir")
    parser.add_argument("--epub")
    parser.add_argument("--text")
    parser.add_argument("--chapters", type=int, default=1, help="how many chapters of the epub to show")
    parser.add_argument("--skip", type=int, default=0, help="chapters to skip first, past the front matter")
    parser.add_argument("--find", help="show the first chapter whose opening contains this, e.g. its title")
    parser.add_argument("--limit", type=int, default=1500,
                        help="cap on words shown, so a long chapter still loads quickly")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()

    if not os.path.exists(PROBE):
        raise SystemExit("dictprobe not built; run: pwsh bin/host-test.ps1 -Target dictprobe")

    text = page_words(args)
    tokens = [t for t in SPLIT.split(text) if t and HAS_LETTER.search(t)]
    if len(tokens) > args.limit:
        # A chapter runs to tens of thousands of words and every distinct one is
        # a lookup; a few pages' worth is what this is for.
        keep = []
        kept = 0
        for token in SPLIT.split(text):
            keep.append(token)
            if token and HAS_LETTER.search(token):
                kept += 1
                if kept >= args.limit:
                    break
        text = "".join(keep)
        tokens = [t for t in SPLIT.split(text) if t and HAS_LETTER.search(t)]
    # Probed as pairs, so a word governed by the particle in front of it is a
    # different lookup from the same word standing alone.
    pairs = list(dict.fromkeys(page_pairs(text)))
    print("%d words on the page, %d distinct in context -- probing" % (len(tokens), len(pairs)))
    hits = run_probe(args.dictdir, pairs)

    definitions = Definitions(args.dictdir)
    found = 0
    for entry in hits.values():
        if entry.get("found"):
            found += 1
            entry["definition"] = definitions.text(entry["offset"], entry["size"])
            entry["gloss"] = gloss(entry["definition"])
    print("%d of %d distinct words resolved" % (found, len(pairs)))

    body = build_page(text, hits)
    page = PAGE.replace("__BODY__", body).replace("__DATA__", json.dumps(hits, ensure_ascii=False))
    encoded = page.encode("utf-8")

    def lookup_one(word):
        """One live probe, for a word the page never held: the suggestions list.

        A word picked off that list was typed, not pointed at, so it has no
        neighbour and is keyed as standing alone."""
        key = pair_key("", word)
        if key in hits and hits[key].get("definition") is not None:
            return hits[key]
        entry = run_probe(args.dictdir, [("", word)]).get(key)
        if entry and entry.get("found"):
            entry["definition"] = definitions.text(entry["offset"], entry["size"])
            entry["gloss"] = gloss(entry["definition"])
        if entry:
            hits[key] = entry
        return entry

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == "/lookup":
                word = (parse_qs(parsed.query).get("w") or [""])[0]
                payload = json.dumps(lookup_one(word) or {"found": False}, ensure_ascii=False).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
                return
            if parsed.path == "/entry":
                # One of the other readings, opened from the brief screen: the
                # candidate already carries where its article lives, so this is a
                # read rather than another lookup.
                query = parse_qs(parsed.query)
                text = definitions.text(int((query.get("o") or ["0"])[0]), int((query.get("s") or ["0"])[0]))
                payload = json.dumps({"definition": text, "gloss": gloss(text)},
                                     ensure_ascii=False).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
                return
            if parsed.path not in ("/", "/index.html"):
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)

        def log_message(self, *ignored):
            pass

    url = "http://127.0.0.1:%d/" % args.port
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print("serving " + url + "  (ctrl+c to stop)")
    if not args.no_open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
