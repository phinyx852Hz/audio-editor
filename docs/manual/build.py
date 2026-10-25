#!/usr/bin/env python3
"""Builds the PHI manual: a hyperlinked web site and a printable PDF.

    python3 docs/manual/build.py            # both
    python3 docs/manual/build.py --site     # web site only
    python3 docs/manual/build.py --pdf      # PDF only

Needs pandoc (Markdown to HTML), BeautifulSoup 4, and WeasyPrint for the PDF.
Output goes to docs/manual/out/ (site/ and PHI-Manual.pdf).

Writing conventions (see README.md in this folder):
  # Chapter title            one per chapter file
  ## Section {#some-id}      give an id to link to it from elsewhere: [text](#some-id)
  [Master Section]{.ix}      an index entry, filed under its own text
  [text]{.ix t="Loudness!Integrated"}   an index entry with a subentry
  ![Caption](images/x.png)   a figure; a missing file shows as "screenshot to come"
  ::: note / tip / important / phi   call-out boxes
  `Ctrl+Shift+E`             shortcuts in backticks are drawn as keys
"""
import argparse
import datetime
import html
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from bs4 import BeautifulSoup, NavigableString

HERE = Path(__file__).resolve().parent
CHAPTERS = HERE / "chapters"
OUT = HERE / "out"

TITLE = "PHI"
SUBTITLE = "Operation Manual"
VERSION_FILE = HERE / "VERSION"

# The book: parts, each with its chapter files (in chapters/, without .md).
# Front matter comes first and is not numbered.
FRONT = ["copyright", "preface", "conventions"]
PARTS = [
    ("Getting Started", ["welcome", "installation", "tour", "keyboard"]),
    ("Editing", ["files", "recording", "editing", "markers", "processing", "spectral", "repair", "warp", "nyquist-macros"]),
    ("Mastering", ["master-section", "effects", "meters", "references-assistant", "export", "batch", "album", "metadata"]),
    ("Producing and Performing", ["find", "mixer", "clip-launcher", "samplers", "instruments", "trackers", "spatial", "visualizer", "performance"]),
    ("Φ", ["phi"]),
    ("Making PHI Yours", ["customising"]),
]
BACK = ["shortcuts", "files-folders", "credits"]

KEY_TOKEN = (r"(?:Ctrl|Shift|Alt|Super|Meta|F\d{1,2}|Enter|Return|Esc|Escape|Space|Tab|Del|Delete|Backspace|Home|End|Insert|"
             r"Left|Right|Up|Down|Page Up|Page Down|PgUp|PgDn|[A-Z0-9]|[`'\";,.\/\[\]\\=\-*+#]|Plus|Minus|wheel|mouse wheel)")
KEY_RE = re.compile(rf"^{KEY_TOKEN}(?:\s*\+\s*{KEY_TOKEN})*$")
MODIFIER_RE = re.compile(r"^(Ctrl|Shift|Alt|Super|Meta)$")


def slug_of(name):
    return re.sub(r"[^a-z0-9-]", "", name.lower())


def read_version():
    if VERSION_FILE.exists():
        return VERSION_FILE.read_text().strip()
    return "draft"


def pandoc(markdown_text):
    result = subprocess.run(
        ["pandoc", "-f", "markdown+bracketed_spans+fenced_divs+pipe_tables+smart",
         "-t", "html5", "--wrap=none", "--no-highlight"],
        input=markdown_text, capture_output=True, text=True, check=True)
    return result.stdout


class Chapter:
    def __init__(self, name, kind, number=None, part=None):
        self.name = name
        self.kind = kind            # front, chapter, back
        self.number = number        # chapter number (or appendix letter)
        self.part = part
        self.file = name + ".html"
        source = CHAPTERS / (name + ".md")
        if not source.exists():
            sys.exit(f"missing chapter file {source}")
        self.soup = BeautifulSoup(pandoc(source.read_text()), "html.parser")
        h1 = self.soup.find("h1")
        self.title = h1.get_text().strip() if h1 else name
        self.sections = []          # (level, id, text)
        self.figures = 0

    @property
    def label(self):
        if self.kind == "chapter":
            return f"{self.number}"
        if self.kind == "back":
            return f"Appendix {self.number}"
        return ""


def keyify(soup):
    """Turns shortcut-looking inline code into keyboard keys."""
    for code in soup.find_all("code"):
        if code.parent and code.parent.name == "pre":
            continue
        text = code.get_text()
        alternatives = [t.strip() for t in re.split(r"\s+/\s+", text)]
        if not all(KEY_RE.match(a) for a in alternatives if a):
            continue
        group = soup.new_tag("span", attrs={"class": "keys"})
        for i, alt in enumerate(alternatives):
            if i:
                group.append(NavigableString(" / "))
            parts = re.split(r"\s*\+\s*(?=\S)", alt) if alt != "+" else ["+"]
            # "Ctrl++" and "Alt+=" style: a trailing plus is the key itself.
            if alt.endswith("++"):
                parts = re.split(r"\s*\+\s*", alt[:-2]) + ["+"]
            for j, p in enumerate(parts):
                if j:
                    group.append(NavigableString("+"))
                kbd = soup.new_tag("kbd")
                kbd.string = p
                group.append(kbd)
        code.replace_with(group)


def build_book():
    book = []
    for name in FRONT:
        book.append(Chapter(name, "front"))
    n = 0
    for part_title, names in PARTS:
        for name in names:
            n += 1
            book.append(Chapter(name, "chapter", n, part_title))
    for i, name in enumerate(BACK):
        book.append(Chapter(name, "back", chr(ord("A") + i)))

    # Unique ids across the book; remember which page each lives on. Each chapter's
    # title takes the chapter's file name as its id, so [x](#mixer) reaches it.
    where = {}
    for ch in book:
        h1 = ch.soup.find("h1")
        if h1:
            h1["id"] = ch.name
            where[ch.name] = ch
            h1["class"] = h1.get("class", []) + ["chapter-title"]
            if ch.label:
                h1["data-label"] = ch.label
    for ch in book:
        for el in ch.soup.find_all(id=True):
            if el.name == "h1":
                continue
            ident = el["id"]
            if ident in where:
                ident = f"{ch.name}-{ident}"
                el["id"] = ident
            where[ident] = ch
        for h in ch.soup.find_all(["h2", "h3"]):
            if not h.get("id"):
                h["id"] = f"{ch.name}-{slug_of(h.get_text())[:40]}"
                where[h["id"]] = ch
            ch.sections.append((int(h.name[1]), h["id"], h.get_text().strip()))
        keyify(ch.soup)

    # Figures: numbered per chapter; a missing image is a placeholder to fill in.
    missing = []
    for ch in book:
        for fig in ch.soup.find_all("figure"):
            img = fig.find("img")
            cap = fig.find("figcaption")
            ch.figures += 1
            number = f"{ch.number}.{ch.figures}" if ch.number else f"{ch.figures}"
            if cap:
                lead = ch.soup.new_tag("span", attrs={"class": "fignum"})
                lead.string = f"Figure {number} "
                cap.insert(0, lead)
            if img is not None:
                src = img.get("src", "")
                if not (HERE / src).exists():
                    missing.append((ch.name, src, cap.get_text() if cap else ""))
                    box = ch.soup.new_tag("div", attrs={"class": "shot-placeholder"})
                    box.string = f"Screenshot to come: {src.split('/')[-1]}"
                    img.replace_with(box)
                else:
                    img["loading"] = "lazy"
            fig["id"] = fig.get("id") or f"fig-{ch.name}-{ch.figures}"
            where[fig["id"]] = ch

    # Index entries.
    entries = []
    counter = 0
    for ch in book:
        for span in ch.soup.find_all("span", class_="ix"):
            counter += 1
            ident = f"ix-{counter}"
            span["id"] = ident
            where[ident] = ch
            term = span.get("data-t") or span.get("t") or span.get_text()
            term = term.strip()
            main, _, sub = term.partition("!")
            # The nearest heading, for the web index's context.
            heading = span.find_previous(["h1", "h2", "h3"])
            entries.append({"main": main.strip(), "sub": sub.strip(), "id": ident, "chapter": ch,
                            "context": heading.get_text().strip() if heading else ch.title})
            for attr in ("t", "data-t"):
                if attr in span.attrs:
                    del span.attrs[attr]
    return book, where, entries, missing


def sort_key(term):
    t = term.lower()
    t = re.sub(r"^[φΦ]\s*", "phi ", t)
    t = re.sub(r"^[^a-z0-9]+", "", t)
    return t


def group_index(entries):
    groups = {}
    for e in entries:
        g = groups.setdefault(e["main"].lower(), {"main": e["main"], "refs": [], "subs": {}})
        if e["sub"]:
            g["subs"].setdefault(e["sub"], []).append(e)
        else:
            g["refs"].append(e)
    ordered = sorted(groups.values(), key=lambda g: sort_key(g["main"]))
    for g in ordered:
        g["subs"] = sorted(g["subs"].items(), key=lambda kv: sort_key(kv[0]))
    return ordered


def letter_of(term):
    k = sort_key(term)
    return k[0].upper() if k and k[0].isalpha() else "#"


# --------------------------------------------------------------------------- site

def site_link(where, ident, current):
    ch = where.get(ident)
    if ch is None:
        return None
    return ("" if ch is current else ch.file) + "#" + ident


def fix_links(soup, where, current, mode):
    for a in soup.find_all("a", href=True):
        href = a["href"]
        if href.startswith("#"):
            ident = href[1:]
            if ident not in where:
                print(f"  warning: {current.name}: link to unknown #{ident}", file=sys.stderr)
                continue
            if mode == "site":
                a["href"] = site_link(where, ident, current)
        elif mode == "site" and href.endswith(".md") and not href.startswith("http"):
            name = Path(href).stem
            if any(ch.name == name for ch in where.values()):
                a["href"] = name + ".html"


def chapter_heading_text(ch):
    if ch.kind == "chapter":
        return f"{ch.number}. {ch.title}"
    if ch.kind == "back":
        return f"{ch.number}. {ch.title}"
    return ch.title


def sidebar(book, current):
    out = ['<nav class="side" aria-label="Contents"><a class="side-home" href="index.html">'
           '<img src="assets/phi-mark-gold.svg" alt=""> <span>PHI Manual</span></a>'
           '<input class="search" type="search" placeholder="Search the manual" aria-label="Search">'
           '<ol class="search-results" hidden></ol><div class="side-toc">']
    part = None
    out.append('<ol class="toc-front">')
    for ch in book:
        if ch.kind != "front":
            continue
        cls = ' class="here"' if ch is current else ""
        out.append(f'<li{cls}><a href="{ch.file}">{html.escape(ch.title)}</a></li>')
    out.append("</ol>")
    for ch in book:
        if ch.kind == "chapter" and ch.part != part:
            if part is not None:
                out.append("</ol>")
            part = ch.part
            out.append(f'<div class="toc-part">{html.escape(part)}</div><ol>')
        if ch.kind == "back" and part != "__back":
            out.append("</ol>")
            part = "__back"
            out.append('<div class="toc-part">Appendices</div><ol>')
        if ch.kind == "front":
            continue
        cls = ' class="here"' if ch is current else ""
        num = f'<span class="n">{ch.number}</span>'
        out.append(f'<li{cls}><a href="{ch.file}">{num}{html.escape(ch.title)}</a>')
        if ch is current and ch.sections:
            out.append('<ol class="local">')
            for level, ident, text in ch.sections:
                if level == 2:
                    out.append(f'<li><a href="#{ident}">{html.escape(text)}</a></li>')
            out.append("</ol>")
        out.append("</li>")
    out.append('</ol><ol class="toc-front"><li' + (' class="here"' if current == "index" else "") +
               '><a href="genindex.html">Index</a></li></ol></div></nav>')
    return "".join(out)


def page(title, body, book, current, version, prev=None, nxt=None, extra_class=""):
    nav = ['<nav class="pager">']
    nav.append(f'<a class="prev" href="{prev[0]}">← {html.escape(prev[1])}</a>' if prev else "<span></span>")
    nav.append(f'<a class="next" href="{nxt[0]}">{html.escape(nxt[1])} →</a>' if nxt else "<span></span>")
    nav.append("</nav>")
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title) if title == "PHI Manual" else html.escape(title) + " · PHI Manual"}</title>
<link rel="icon" href="assets/phi.svg">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=EB+Garamond:ital,wght@0,400;0,500;0,600;1,400&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap">
<link rel="stylesheet" href="assets/manual.css">
<link rel="stylesheet" href="assets/site.css">
</head>
<body class="{extra_class}">
<button class="menu-toggle" aria-label="Contents">☰</button>
{sidebar(book, current)}
<main>
<header class="topline"><span>PHI · {html.escape(SUBTITLE)}</span><span>{html.escape(version)}</span></header>
{body}
{"".join(nav)}
<footer class="foot">© 2026 phinyx852Hz. PHI and the Φ mark are the author's. <a href="copyright.html">Copyright and licences</a></footer>
</main>
<script src="assets/site.js"></script>
</body>
</html>
"""


def cover_html(version, for_print):
    date = datetime.date.today().strftime("%B %Y")
    mark = "assets/phi-mark-gold.svg"
    return f"""<section class="cover">
<div class="cover-inner">
<img class="cover-mark" src="{mark}" alt="Φ">
<div class="cover-title">PHI</div>
<div class="cover-sub">Open Source DAW</div>
<div class="cover-sub cover-sub2">Audio Editor · Mastering Suite · Mixer</div>
<div class="cover-rule"></div>
<div class="cover-book">{SUBTITLE}</div>
<div class="cover-version">{html.escape(version)} · {date}</div>
</div>
</section>"""


def contents_html(book, mode, pages=None):
    out = ['<section class="contents"><h1 class="contents-title" id="contents">Contents</h1><ol class="toc">']
    part = None
    for ch in book:
        if ch.kind == "front" and ch.name == "copyright":
            continue
        if ch.kind == "chapter" and ch.part != part:
            part = ch.part
            out.append(f'<li class="toc-part-title">{html.escape(part)}</li>')
        if ch.kind == "back" and part != "__back":
            part = "__back"
            out.append('<li class="toc-part-title">Appendices</li>')
        href = (ch.file + "#" + ch.name) if mode == "site" else "#" + ch.name
        num = f'<span class="n">{ch.number}</span>' if ch.number else '<span class="n"></span>'
        out.append(f'<li class="toc-ch toc-{ch.kind}"><a href="{href}">{num}<span class="t">{html.escape(ch.title)}</span>{pg(pages, ch.name, mode)}</a>')
        if ch.kind != "front":
            subs = [s for s in ch.sections if s[0] == 2]
            if subs:
                out.append('<ol>')
                for _, ident, text in subs:
                    h = (ch.file + "#" + ident) if mode == "site" else "#" + ident
                    out.append(f'<li><a href="{h}"><span class="t">{html.escape(text)}</span>{pg(pages, ident, mode)}</a></li>')
                out.append("</ol>")
        out.append("</li>")
    h = "genindex.html" if mode == "site" else "#index"
    out.append(f'<li class="toc-ch toc-back"><a href="{h}"><span class="n"></span><span class="t">Index</span>{pg(pages, "index", mode)}</a></li>')
    out.append("</ol></section>")
    return "".join(out)


def pg(pages, ident, mode):
    if mode != "print":
        return ""
    number = pages.get(ident, "") if pages else "00"
    return f'<span class="dots"></span><span class="pg">{number}</span>'


def index_html(entries, where, mode, pages=None):
    groups = group_index(entries)
    letters = []
    out = []
    current_letter = None

    def refs_html(refs):
        links = []
        seen = set()
        for e in refs:
            if mode == "site":
                href = e["chapter"].file + "#" + e["id"]
                links.append(f'<a href="{href}" title="{html.escape(e["chapter"].title)}">{html.escape(e["context"])}</a>')
            else:
                p = pages.get(e["id"]) if pages else None
                if pages is not None:
                    if p is None or p in seen:
                        continue
                    seen.add(p)
                    links.append(f'<a href="#{e["id"]}">{p}</a>')
                else:
                    links.append(f'<a class="ixref" href="#{e["id"]}"></a>')
        return ", ".join(links)

    for g in groups:
        letter = letter_of(g["main"])
        if letter != current_letter:
            if current_letter is not None:
                out.append("</dl>")
            current_letter = letter
            letters.append(letter)
            out.append(f'<h2 class="ix-letter" id="ix-letter-{letter}">{letter}</h2><dl class="ix">')
        out.append(f'<dt>{html.escape(g["main"])}')
        if g["refs"]:
            out.append(f' <span class="ix-refs">{refs_html(g["refs"])}</span>')
        out.append("</dt>")
        for sub, refs in g["subs"]:
            out.append(f'<dd>{html.escape(sub)} <span class="ix-refs">{refs_html(refs)}</span></dd>')
    if current_letter is not None:
        out.append("</dl>")
    jump = "".join(f'<a href="#ix-letter-{l}">{l}</a>' for l in letters)
    head = '<h1 class="chapter-title" id="index">Index</h1>'
    if mode == "site":
        head += f'<nav class="ix-jump">{jump}</nav>'
    return f'<section class="index">{head}<div class="ix-cols">{"".join(out)}</div></section>'


def search_data(book):
    items = []
    for ch in book:
        items.append({"t": ch.title, "c": chapter_heading_text(ch), "h": ch.file})
        for level, ident, text in ch.sections:
            items.append({"t": text, "c": chapter_heading_text(ch), "h": ch.file + "#" + ident})
    return items


def copy_assets(dest):
    (dest / "assets").mkdir(parents=True, exist_ok=True)
    for f in (HERE / "assets").iterdir():
        shutil.copy2(f, dest / "assets" / f.name)
    if (HERE / "images").exists():
        shutil.copytree(HERE / "images", dest / "images", dirs_exist_ok=True)


def build_site(book, where, entries, version):
    dest = OUT / "site"
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    copy_assets(dest)
    order = [(ch.file, ch.title) for ch in book] + [("genindex.html", "Index")]

    home = (cover_html(version, False)
            + '<p class="pdf-link"><a href="PHI-Manual.pdf">Download the whole manual as a PDF</a></p>'
            + contents_html(book, "site"))
    (dest / "index.html").write_text(page("PHI Manual", home, book, None, version, None, order[0], "home"))
    for i, ch in enumerate(book):
        soup = BeautifulSoup(str(ch.soup), "html.parser")
        fix_links(soup, where, ch, "site")
        prev = order[i - 1] if i > 0 else ("index.html", "Contents")
        nxt = order[i + 1]
        body = f'<article class="chapter chapter-{ch.kind}">{soup}</article>'
        (dest / ch.file).write_text(page(ch.title, body, book, ch, version, prev, nxt))
    (dest / "genindex.html").write_text(page("Index", index_html(entries, where, "site"), book, "index", version,
                                             order[-2], None))
    (dest / "assets" / "search.js").write_text("window.PHI_SEARCH = " + json.dumps(search_data(book), ensure_ascii=False) + ";")
    print(f"site: {dest}/index.html ({len(book) + 2} pages)")


# --------------------------------------------------------------------------- print

def print_document(book, where, entries, version, pages=None):
    parts = [cover_html(version, True)]
    copyright_ch = next(ch for ch in book if ch.name == "copyright")
    parts.append(f'<article class="chapter chapter-front copyright-page">{copyright_ch.soup}</article>')
    parts.append(contents_html(book, "print", pages))
    part = None
    for ch in book:
        if ch is copyright_ch:
            continue
        if ch.kind == "chapter" and ch.part != part:
            part = ch.part
            n = [p[0] for p in PARTS].index(part) + 1
            parts.append(f'<section class="part-page"><div class="part-num">Part {roman(n)}</div>'
                         f'<div class="part-title">{html.escape(part)}</div></section>')
        if ch.kind == "back" and part != "__back":
            part = "__back"
            parts.append('<section class="part-page"><div class="part-num">&#160;</div>'
                         '<div class="part-title">Appendices</div></section>')
        soup = BeautifulSoup(str(ch.soup), "html.parser")
        fix_links(soup, where, ch, "print")
        parts.append(f'<article class="chapter chapter-{ch.kind}">{soup}</article>')
    parts.append(index_html(entries, where, "print", pages))
    body = "\n".join(parts)
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<title>PHI {SUBTITLE}</title>
<meta name="author" content="phinyx852Hz">
<meta name="description" content="PHI, the open-source DAW: operation manual">
<link rel="stylesheet" href="assets/manual.css"><link rel="stylesheet" href="assets/print.css">
</head><body class="print">{body}</body></html>"""


def roman(n):
    vals = [(10, "X"), (9, "IX"), (5, "V"), (4, "IV"), (1, "I")]
    out = ""
    for v, s in vals:
        while n >= v:
            out += s
            n -= v
    return out


def build_pdf(book, where, entries, version):
    from weasyprint import HTML
    dest = OUT
    dest.mkdir(parents=True, exist_ok=True)
    work = OUT / "print"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir()
    copy_assets(work)
    source = work / "manual.html"
    # Pass 1 finds the page of every index anchor; pass 2 writes them, without repeats.
    source.write_text(print_document(book, where, entries, version))
    doc = HTML(filename=str(source)).render()
    pages = {}
    for number, pg in enumerate(doc.pages, start=1):
        for anchor in pg.anchors:
            pages.setdefault(anchor, number)
    source.write_text(print_document(book, where, entries, version, pages))
    doc = HTML(filename=str(source)).render()
    pdf = dest / "PHI-Manual.pdf"
    doc.write_pdf(str(pdf))
    site = dest / "site"
    if site.is_dir():
        shutil.copy(pdf, site / pdf.name)
    print(f"pdf: {pdf} ({len(doc.pages)} pages)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--site", action="store_true")
    ap.add_argument("--pdf", action="store_true")
    args = ap.parse_args()
    both = not (args.site or args.pdf)
    version = read_version()
    book, where, entries, missing = build_book()
    if both or args.site:
        build_site(book, where, entries, version)
    if both or args.pdf:
        build_pdf(book, where, entries, version)
    (OUT / "missing-screenshots.txt").write_text(
        "".join(f"{c}\t{s}\t{t}\n" for c, s, t in missing))
    print(f"{len(entries)} index entries, {len(missing)} screenshots still to come")


if __name__ == "__main__":
    main()
