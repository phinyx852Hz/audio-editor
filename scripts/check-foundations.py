#!/usr/bin/env python3
"""Check that docs/foundations.md names everything the build brings in from others.

Usage: python3 scripts/check-foundations.py

Fails (exit 1), listing what is missing, when docs/foundations.md does not mention:
- a repository the build fetches (GIT_REPOSITORY, phi_juce_instrument, git clone) in
  CMakeLists.txt, Source/core/CMakeLists.txt or cmake/*.cmake, by its owner/name;
- a pkg-config module the build looks for, in backticks;
- a folder under third_party/, as third_party/<folder>;
- a stock plugin's licence name (phi_stock_licence), in backticks;
- a file under Source/ whose SPDX licence is not AGPL-3.0-only, or that says it is
  adapted or ported from something, or carries a copyright line, by its path.
It also fails for any file in Source/ or third_party/ marked GPL-2.0-only, which
cannot be combined with PHI's AGPL-3.0-only.

${...} in a fetched URL matches any name. Uses only the standard library.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.path.join(ROOT, "docs", "foundations.md")
SOURCE_EXTS = (".cpp", ".h", ".c", ".hpp", ".py", ".cmake", ".txt")


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def build_files():
    files = [os.path.join(ROOT, "CMakeLists.txt"), os.path.join(ROOT, "Source", "core", "CMakeLists.txt")]
    cmake = os.path.join(ROOT, "cmake")
    files += [os.path.join(cmake, f) for f in sorted(os.listdir(cmake)) if f.endswith(".cmake")]
    return [f for f in files if os.path.exists(f)]


def as_pattern(text):
    # ${zl_name} and the like stand for any name
    parts = re.split(r"\$\{[^}]*\}", text)
    return r"[\w.-]+".join(re.escape(p) for p in parts)


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def main():
    doc = read(DOC)
    doc_lower = doc.lower()
    missing = []

    for path in build_files():
        text = read(path)
        where = rel(path)
        urls = re.findall(r"(?:GIT_REPOSITORY|git clone -q|phi_juce_instrument\(\S+)\s+(https?://\S+?)(?:\.git)?(?=[\s)])", text)
        for url in urls:
            m = re.match(r"https?://[^/]+/([^/\s]+/[^/\s]+)", url)
            if not m:
                continue
            repo = m.group(1)
            if not re.search(as_pattern(repo.lower()), doc_lower):
                missing.append(f"repository {repo} ({where})")
        for call in re.findall(r"pkg_check_modules\(([^)]*)\)", text):
            words = call.split()
            modules = [w for w in words[1:] if w not in ("REQUIRED", "QUIET", "IMPORTED_TARGET", "GLOBAL", "NO_CMAKE_PATH", "NO_CMAKE_ENVIRONMENT_PATH")]
            for module in modules:
                name = re.split(r"[<>=]", module)[0]
                if f"`{name}`" not in doc:
                    missing.append(f"pkg-config module `{name}` ({where})")
        names = re.findall(r"phi_stock_licence\(\s*([^\s)]+)", text) + re.findall(r"^phi_juce_instrument\(\s*([^\s)]+)", text, re.MULTILINE)
        for name in names:
            if name.startswith("${"):
                continue   # the name a function was given: checked where it is called
            if not re.search("`" + as_pattern(name) + "`", doc):
                missing.append(f"stock licence name `{name}` ({where})")

    third = os.path.join(ROOT, "third_party")
    if os.path.isdir(third):
        for entry in sorted(os.listdir(third)):
            if os.path.isdir(os.path.join(third, entry)) and f"third_party/{entry}" not in doc:
                missing.append(f"folder third_party/{entry}")

    gpl2_only = []
    marker = re.compile(r"\b(adapted|ported) from\b|\bcopyright (\(c\) |©\s*)?(19|20)\d\d", re.IGNORECASE)
    for top in ("Source", "third_party"):
        for dirpath, _, names in os.walk(os.path.join(ROOT, top)):
            for n in sorted(names):
                path = os.path.join(dirpath, n)
                if not n.endswith(SOURCE_EXTS):
                    continue
                text = read(path)
                head = text[:4000]
                if re.search(r"SPDX-License-Identifier:\s*GPL-2\.0(-only)?\b(?!-or-later|\+)", head):
                    gpl2_only.append(rel(path))
                if top != "Source":
                    continue
                spdx = re.search(r"SPDX-License-Identifier:\s*(\S+)", head)
                foreign = spdx is not None and spdx.group(1) != "AGPL-3.0-only"
                if (foreign or marker.search(text)) and rel(path) not in doc:
                    missing.append(f"file {rel(path)} (credits other work)")

    ok = True
    if missing:
        ok = False
        print("docs/foundations.md does not mention:")
        for m in missing:
            print(f"  - {m}")
        print("Add a row for each (see 'Keeping this file current' at the top of docs/foundations.md).")
    if gpl2_only:
        ok = False
        print("GPL-2.0-only files cannot be combined with PHI's AGPL-3.0-only:")
        for g in gpl2_only:
            print(f"  - {g}")
    if ok:
        print("docs/foundations.md names every fetched repository, pkg-config module, third_party folder, stock licence and credited file.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
