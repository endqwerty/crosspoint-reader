#!/usr/bin/env python3
"""Count EPUB patterns that open upstream parser fixes target, without modifying books.

Usage: scripts/scan-epub-library.py [--json OUT.json] [--examples N] LIBRARY_DIR

Walks LIBRARY_DIR (for example a Calibre "Save to disk" export) for *.epub files
and reports, per pattern, how many books and occurrences are affected:

  pagebreak-text      pagebreak marker (span/div/a...) wrapping book text (#3349)
  pagebreak-block     p/h1-h6/li/blockquote tagged as a pagebreak holding text (#3349)
  malformed-xhtml     spine document Expat rejects; void-* marks unclosed void tags (#3375)
  toc-filename-only   TOC href that resolves to a spine item only by file name (#2987)
  toc-unresolved      TOC href matching no spine item at all
  nested-landmarks    landmarks <nav> nested inside the toc <nav> (#2297)
  svg-cover           manifest cover-image that is an SVG/XHTML wrapper (#3539)
  zip-comment         ZIP comment too long for the 1 KB end-of-archive scan (#2614)
  image-extension     image whose extension does not name its format (#2386)

Chapters are parsed with Expat configured like the reader (no namespace
processing, unknown HTML entities passed through), so "malformed" means the
reader's parser would reject the chapter too. Only reads the files.
"""

import argparse
import json
import os
import posixpath
import re
import sys
import zipfile
from urllib.parse import unquote
from xml.etree import ElementTree
from xml.parsers import expat

TEXT_BLOCKS = {"p", "blockquote", "li", "h1", "h2", "h3", "h4", "h5", "h6"}
VOID_TAG = re.compile(rb"<(br|hr|img|meta|link|input|col|wbr|area|base|source|embed|param|track)\b[^>]*(?<!/)>", re.I)
IMAGE_EXTENSIONS = {".jpg": "jpeg", ".jpeg": "jpeg", ".png": "png", ".gif": "gif", ".bmp": "bmp", ".webp": "webp",
                    ".svg": "svg"}
# The reader finds the end-of-central-directory record only in the last 1024 bytes.
ZIP_EOCD_SCAN = 1024
ZIP_EOCD_SIZE = 22
NS = {"opf": "http://www.idpf.org/2007/opf", "container": "urn:oasis:names:tc:opendocument:xmlns:container",
      "ncx": "http://www.daisy.org/z3986/2005/ncx/"}


def is_pagebreak(attrs):
    return attrs.get("role") == "doc-pagebreak" or attrs.get("epub:type") == "pagebreak"


def roman(text):
    return re.fullmatch(r"(cm|cd|d?c{0,3})(xc|xl|l?x{0,3})(ix|iv|v?i{0,3})", text) is not None


def label_key(text):
    return "".join(c for c in text.lower() if c.isascii() and c.isalnum())


def is_label_text(text, label):
    """Mirror of the reader's isPagebreakLabelText()."""
    text = text.strip(" \t\r\n")
    if not text:
        return True
    if label:
        if text == label:
            return True
        key = label_key(label)
        return bool(key) and label_key(text) in (key, "p" + key, "page" + key)
    return len(text.encode()) <= 8 and (text.isascii() and text.isdigit() or roman(text))


def image_format(data):
    if data.startswith(b"\xff\xd8"):
        return "jpeg"
    if data.startswith(b"\x89PNG"):
        return "png"
    if data[:6] in (b"GIF87a", b"GIF89a"):
        return "gif"
    if data.startswith(b"BM"):
        return "bmp"
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "webp"
    if b"<svg" in data[:512]:
        return "svg"
    return "unknown"


class ChapterScan:
    """Expat pass over one spine document, tracking pagebreak markers."""

    def __init__(self):
        self.markers = []  # open marker frames: {"depth", "label", "text", "children"}
        self.depth = 0
        self.text_markers = 0
        self.text_blocks = 0
        self.lost_chars = 0

    def start(self, name, attrs):
        for frame in self.markers:
            if name != "br":
                frame["children"] = True
        self.depth += 1
        if is_pagebreak(attrs):
            label = attrs.get("aria-label") or attrs.get("title") or ""
            self.markers.append({"depth": self.depth, "block": name in TEXT_BLOCKS, "label": label, "text": [],
                                 "children": False})

    def end(self, _name):
        if self.markers and self.markers[-1]["depth"] == self.depth:
            frame = self.markers.pop()
            text = "".join(frame["text"])
            if frame["block"]:
                if text.strip():
                    self.text_blocks += 1
                    self.lost_chars += len(text.strip())
            elif frame["children"] or not is_label_text(text, frame["label"]):
                if text.strip():
                    self.text_markers += 1
                    self.lost_chars += len(text.strip())
            if self.markers:
                self.markers[-1]["text"].append(text)
        self.depth -= 1

    def chars(self, data):
        if self.markers:
            self.markers[-1]["text"].append(data)

    def default(self, data):
        # Unknown entities are passed through as text, like the reader.
        if len(data) >= 3 and data.startswith("&") and data.endswith(";"):
            self.chars(data)

    def parse(self, data):
        parser = expat.ParserCreate()
        parser.StartElementHandler = self.start
        parser.EndElementHandler = self.end
        parser.CharacterDataHandler = self.chars
        parser.DefaultHandlerExpand = self.default
        parser.Parse(data, True)


class TocScan:
    def __init__(self):
        self.hrefs = []
        self.nested_landmarks = 0
        self.stack = []  # nav types open

    def start(self, name, attrs):
        if name == "nav":
            types = (attrs.get("epub:type") or "").split()
            if "landmarks" in types and "toc" in self.stack:
                self.nested_landmarks += 1
            self.stack.append("toc" if "toc" in types else "landmarks" if "landmarks" in types else "other")
        elif name == "a" and self.stack and self.stack[-1] == "toc" and attrs.get("href"):
            self.hrefs.append(attrs["href"])

    def end(self, name):
        if name == "nav" and self.stack:
            self.stack.pop()


def resolve(base_dir, href):
    path = unquote(href.split("#", 1)[0])
    return posixpath.normpath(posixpath.join(base_dir, path)) if path else ""


def scan_book(path):
    found = {}

    def hit(pattern, count=1, detail=None):
        entry = found.setdefault(pattern, {"count": 0, "details": []})
        entry["count"] += count
        if detail and len(entry["details"]) < 5:
            entry["details"].append(detail)

    with zipfile.ZipFile(path) as book:
        if len(book.comment) > ZIP_EOCD_SCAN - ZIP_EOCD_SIZE:
            hit("zip-comment", detail=f"{len(book.comment)} byte comment")
        names = set(book.namelist())
        container = ElementTree.fromstring(book.read("META-INF/container.xml"))
        opf_path = container.find(".//container:rootfile", NS).get("full-path")
        opf_dir = posixpath.dirname(opf_path)
        opf = ElementTree.fromstring(book.read(opf_path))
        manifest = {}
        for item in opf.findall(".//opf:manifest/opf:item", NS):
            manifest[item.get("id")] = {"path": resolve(opf_dir, item.get("href", "")),
                                        "type": item.get("media-type", ""),
                                        "properties": (item.get("properties") or "").split()}
        spine_node = opf.find(".//opf:spine", NS)
        spine = [manifest[ref.get("idref")]["path"] for ref in opf.findall(".//opf:spine/opf:itemref", NS)
                 if ref.get("idref") in manifest]
        spine_set = set(spine)
        spine_names = {posixpath.basename(p) for p in spine}

        for item in manifest.values():
            if "cover-image" in item["properties"] and item["type"] in ("image/svg+xml", "application/xhtml+xml"):
                hit("svg-cover", detail=item["path"])
            if item["type"].startswith("image/") and item["path"] in names:
                extension = posixpath.splitext(item["path"])[1].lower()
                with book.open(item["path"]) as image:
                    actual = image_format(image.read(512))
                if IMAGE_EXTENSIONS.get(extension) != actual and actual in ("jpeg", "png"):
                    hit("image-extension", detail=f"{item['path']} is {actual}")

        toc_hrefs = []
        for item in manifest.values():
            if "nav" in item["properties"] and item["path"] in names:
                scan = TocScan()
                parser = expat.ParserCreate()
                parser.StartElementHandler = scan.start
                parser.EndElementHandler = scan.end
                try:
                    parser.Parse(book.read(item["path"]), True)
                except expat.ExpatError:
                    continue
                if scan.nested_landmarks:
                    hit("nested-landmarks", scan.nested_landmarks, item["path"])
                toc_hrefs += [(posixpath.dirname(item["path"]), h) for h in scan.hrefs]
        ncx_id = spine_node.get("toc") if spine_node is not None else None
        if not toc_hrefs and ncx_id in manifest and manifest[ncx_id]["path"] in names:
            ncx_path = manifest[ncx_id]["path"]
            ncx = ElementTree.fromstring(book.read(ncx_path))
            toc_hrefs = [(posixpath.dirname(ncx_path), c.get("src", ""))
                         for c in ncx.findall(".//ncx:navPoint/ncx:content", NS)]
        for base, href in toc_hrefs:
            target = resolve(base, href)
            if not target or target in spine_set:
                continue
            if posixpath.basename(target) in spine_names:
                hit("toc-filename-only", detail=href)
            else:
                hit("toc-unresolved", detail=href)

        lost_chars = 0
        for chapter in spine:
            if chapter not in names:
                continue
            data = book.read(chapter)
            scan = ChapterScan()
            try:
                scan.parse(data)
            except expat.ExpatError as error:
                void = VOID_TAG.search(data)
                kind = "void-" + void.group(1).decode().lower() if void else expat.ErrorString(error.code)
                hit("malformed-xhtml", detail=f"{chapter}: {kind}")
                continue
            if scan.text_markers:
                hit("pagebreak-text", scan.text_markers, chapter)
            if scan.text_blocks:
                hit("pagebreak-block", scan.text_blocks, chapter)
            lost_chars += scan.lost_chars
        if lost_chars:
            found.setdefault("pagebreak-text-chars", {"count": 0, "details": []})["count"] += lost_chars
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("library")
    parser.add_argument("--json", help="also write per-book results to this file")
    parser.add_argument("--examples", type=int, default=3, help="example books listed per pattern")
    args = parser.parse_args()

    books = sorted(os.path.join(root, name) for root, _, files in os.walk(args.library) for name in files
                   if name.lower().endswith(".epub") and not name.startswith("."))
    results = {}
    errors = {}
    for index, path in enumerate(books, 1):
        relative = os.path.relpath(path, args.library)
        try:
            results[relative] = scan_book(path)
        except (zipfile.BadZipFile, KeyError, ElementTree.ParseError, AttributeError, OSError) as error:
            errors[relative] = f"{type(error).__name__}: {error}"
        if index % 50 == 0:
            print(f"scanned {index}/{len(books)}", file=sys.stderr)

    patterns = {}
    for book, found in results.items():
        for pattern, entry in found.items():
            summary = patterns.setdefault(pattern, {"books": 0, "occurrences": 0, "examples": []})
            summary["books"] += 1
            summary["occurrences"] += entry["count"]
            if len(summary["examples"]) < args.examples:
                summary["examples"].append({"book": book, "details": entry["details"][:2]})

    print(f"Scanned {len(results)} of {len(books)} EPUBs ({len(errors)} could not be read)")
    order = ["pagebreak-text", "pagebreak-block", "pagebreak-text-chars", "malformed-xhtml", "toc-filename-only",
             "toc-unresolved", "nested-landmarks", "svg-cover", "zip-comment", "image-extension"]
    for pattern in order:
        summary = patterns.get(pattern, {"books": 0, "occurrences": 0, "examples": []})
        print(f"{pattern:22} books {summary['books']:4}  occurrences {summary['occurrences']}")
        for example in summary["examples"]:
            print(f"    {example['book']}: {'; '.join(example['details'])}")
    for book, error in list(errors.items())[:args.examples]:
        print(f"unreadable: {book}: {error}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as out:
            json.dump({"patterns": patterns, "books": results, "errors": errors}, out, indent=1, ensure_ascii=False)


if __name__ == "__main__":
    main()
