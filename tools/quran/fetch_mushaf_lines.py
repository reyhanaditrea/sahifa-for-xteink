"""Fetch the Madinah Mushaf's line breaks and write source/mushaf_lines.json.

build_quran_epub_kfgqpc.py sets each Mushaf line as its own paragraph, so the
reader's lines end where the printed page's do. The KFGQPC source carries page
breaks but no line breaks; quran.com's API has them per word (line_number on
mushaf=5, KFGQPC Hafs -- checked against the printed pages: page 3's first
line ends on تُنذِرۡهُمۡ, as in print).

Output: {"<surah>": [[ayah, word], ...]} -- where each Mushaf line of that surah
begins, as an index into the ayah's words in OUR source text (word == the
ayah's word count means the line opens on its ayah marker). Lines are listed in
order; a surah's first line is included.

The API's word split differs from the KFGQPC text's on 7 ayahs (by a word);
there the break is mapped by position along the ayah, so it can land a word off.

Usage: python3 fetch_mushaf_lines.py [cache_dir]   (from tools/quran/; needs network)
"""

import html
import json
import re
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE_DIR = HERE / "source" / "kfgqpc_chapters"
OUT = HERE / "source" / "mushaf_lines.json"
API = "https://api.quran.com/api/v4/verses/by_page/{}?words=true&word_fields=line_number,text_uthmani&per_page=50&mushaf=5"


def our_words() -> dict[tuple[int, int], list[str]]:
    """Each ayah's words in the KFGQPC chapter text, as the EPUB build splits them."""
    words = {}
    for surah in range(1, 115):
        text = (SOURCE_DIR / f"chapter-{surah}.xhtml").read_text(encoding="utf-8")
        body = text[text.index(">", text.index("surah-text")) + 1 :]
        body = re.sub(r'<span epub:type="pagebreak"[^>]*></span>', "", body)
        for m in re.finditer(r'(.*?)<span class="ayah-number" id="ayah-%d-(\d+)">[^<]*</span>' % surah, body, re.S):
            ayah_text = html.unescape(re.sub(r"<[^>]+>", "", m.group(1)))
            words[(surah, int(m.group(2)))] = ayah_text.replace("⁠", "").replace("​", " ").split()
    return words


def fetch_page(page: int, cache: Path | None) -> dict:
    cached = cache / f"{page}.json" if cache else None
    if cached and cached.exists():
        return json.loads(cached.read_text(encoding="utf-8"))
    with urllib.request.urlopen(API.format(page)) as response:
        data = json.loads(response.read().decode("utf-8"))
    if cached:
        cached.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
    time.sleep(0.1)
    return data


def main() -> int:
    cache = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if cache:
        cache.mkdir(parents=True, exist_ok=True)
    ours = our_words()
    starts: dict[str, list[list[int]]] = {}
    exact = 0
    for page in range(1, 605):
        verses = fetch_page(page, cache)["verses"]
        assert verses and fetch_page(page, cache)["pagination"]["next_page"] is None, page
        for verse in verses:
            surah, ayah = map(int, verse["verse_key"].split(":"))
            mine = ours[(surah, ayah)]
            api_words = [w for w in verse["words"] if w["char_type_name"] == "word"]
            if len(api_words) == len(mine):
                mapping = list(range(len(mine)))
                exact += 1
            else:
                # Map each API word to ours by its position along the ayah.
                total_api = sum(len(w["text_uthmani"]) for w in api_words)
                total_ours = sum(len(w) for w in mine)
                at_ours, pos = [], 0
                for w in mine:
                    at_ours.append(pos / total_ours)
                    pos += len(w)
                mapping, pos = [], 0
                for w in api_words:
                    frac = pos / total_api
                    pos += len(w["text_uthmani"])
                    mapping.append(min(range(len(mine)), key=lambda i: abs(at_ours[i] - frac)))
            line_of = [(w["line_number"], mapping[i]) for i, w in enumerate(api_words)]
            line_of.append((verse["words"][-1]["line_number"], len(mine)))  # the ayah marker
            surah_starts = starts.setdefault(str(surah), [])
            prev_line = None
            for line, word in line_of:
                key = (page, line)
                if key != prev_line and (not surah_starts or surah_starts[-1][2:] != list(key)):
                    surah_starts.append([ayah, word, page, line])
                prev_line = key
    out = {s: [[a, w] for a, w, _, _ in v] for s, v in starts.items()}
    OUT.write_text(json.dumps(out, separators=(",", ":")), encoding="utf-8")
    print(f"wrote {OUT} ({sum(len(v) for v in out.values())} lines; {exact}/{len(ours)} ayahs split word-for-word)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
