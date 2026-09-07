"""Extract every word of an EPUB the way the reader's layout pass would.

Writes one UTF-8 token per line, for `dictprobe <dict> --words <file>`.

The split is deliberately the reader's, not a sensible one: the layout parser
breaks character data on ASCII space, tab, CR and LF only
(lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp), so punctuation, tatweel,
diacritics and zero-width joiners all stay inside the token, and dictprobe then
applies the same cleanWord() the device does. Getting this wrong in either
direction would make the audit measure the wrong thing.

Not reproduced, and each of them can only add words rather than lose one:
CSS visibility (display:none content is included here), ruby and footnote-label
suppression, and the 200-byte hard split on an over-long token.

    python tools/dictprobe/epub_words.py book.epub words.txt
    python tools/dictprobe/epub_words.py book.epub words.txt --unique
"""

import argparse
import html.parser
import posixpath
import re
import sys
import xml.etree.ElementTree as ElementTree
import zipfile

CONTAINER_PATH = "META-INF/container.xml"
# Whatever the reader turns into a space or drops before splitting.
NBSP = " "
NARROW_NBSP = " "
BYTE_ORDER_MARK = "﻿"
SPLIT = re.compile(r"[ \t\r\n]+")


class TextCollector(html.parser.HTMLParser):
    """Character data outside head, script and style, with entities resolved."""

    SKIP = {"head", "script", "style", "title"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
        self.depth = 0

    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP:
            self.depth += 1

    def handle_endtag(self, tag):
        if tag in self.SKIP and self.depth > 0:
            self.depth -= 1

    def handle_data(self, data):
        if self.depth == 0:
            self.parts.append(data)


def spine_documents(book):
    """The XHTML files, in reading order, via container.xml and the OPF spine."""
    container = ElementTree.fromstring(book.read(CONTAINER_PATH))
    rootfile = container.find(".//{urn:oasis:names:tc:opendocument:xmlns:container}rootfile")
    if rootfile is None:
        raise SystemExit("no rootfile in " + CONTAINER_PATH)
    opf_path = rootfile.attrib["full-path"]
    opf_dir = posixpath.dirname(opf_path)

    opf = ElementTree.fromstring(book.read(opf_path))
    ns = {"opf": "http://www.idpf.org/2007/opf"}
    manifest = {
        item.attrib["id"]: item.attrib["href"]
        for item in opf.findall("opf:manifest/opf:item", ns)
    }
    documents = []
    for ref in opf.findall("opf:spine/opf:itemref", ns):
        href = manifest.get(ref.attrib.get("idref", ""))
        if not href:
            continue
        documents.append(posixpath.normpath(posixpath.join(opf_dir, href)) if opf_dir else href)
    return documents


def words_in(book, path):
    collector = TextCollector()
    collector.feed(book.read(path).decode("utf-8", "replace"))
    text = "".join(collector.parts)
    # The reader gives NBSP and narrow NBSP their own space token and drops the
    # byte order mark; for a word list that is the same as substituting them.
    text = text.replace(NBSP, " ").replace(NARROW_NBSP, " ").replace(BYTE_ORDER_MARK, "")
    return [token for token in SPLIT.split(text) if token]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("epub")
    parser.add_argument("out")
    parser.add_argument("--unique", action="store_true", help="one line per distinct token")
    args = parser.parse_args()

    words = []
    with zipfile.ZipFile(args.epub) as book:
        names = set(book.namelist())
        for path in spine_documents(book):
            if path not in names:
                print("missing from the archive, skipped: " + path, file=sys.stderr)
                continue
            words.extend(words_in(book, path))

    if args.unique:
        seen = set()
        unique = []
        for word in words:
            if word not in seen:
                seen.add(word)
                unique.append(word)
        words = unique

    with open(args.out, "w", encoding="utf-8", newline="\n") as out:
        out.write("\n".join(words))
    print("%d words -> %s" % (len(words), args.out))


if __name__ == "__main__":
    main()
