#!/usr/bin/env python3
"""Writes chapters/shortcuts.md from data/commands.tsv (id, category, name, keys).

data/commands.tsv is dumped from a PHI build, so the appendix lists exactly the
commands and default keys that build has. Run build.py afterwards.
"""
import collections
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent.parent
ORDER = ["File", "Edit", "Selection", "Process", "Repair", "Effect", "Generate", "Spatial", "Warp",
         "Markers", "Album", "Analyze", "References", "Tools", "Master Section", "Transport", "View",
         "Mixer", "Sample", "Synth", "Tracker", "Performance", "Options", "Help"]
NAMES = {"Meta": "Super", "PgDown": "Page Down", "PgUp": "Page Up", "Return": "Enter",
         "Ins": "Insert", "Del": "Delete", "Esc": "Esc"}


def key_name(combo):
    trailing_plus = combo.endswith("++")
    parts = combo[:-2].split("+") if trailing_plus else combo.split("+")
    parts = [NAMES.get(p, p) for p in parts]
    if trailing_plus:
        parts.append("+")
    return "+".join(parts)


def keys_cell(keys):
    if not keys:
        return ""
    out = []
    for combo in (k.strip() for k in keys.split(" / ")):
        # Keypad and shifted duplicates of a key already listed add nothing to read.
        if not combo or "Backtab" in combo or "Num+" in combo or combo.endswith("+_"):
            continue
        name = key_name(combo)
        if name not in out:
            out.append(name)
    return " / ".join(f"`{k}`" for k in out)


def main():
    groups = collections.OrderedDict()
    for line in (HERE / "data" / "commands.tsv").read_text().splitlines():
        cid, category, name, keys = (line.split("\t") + [""] * 4)[:4]
        groups.setdefault(category, []).append((name, keys_cell(keys)))
    categories = sorted(groups, key=lambda c: (ORDER.index(c) if c in ORDER else len(ORDER), c))
    total = sum(len(v) for v in groups.values())

    lines = ["# Keyboard Shortcuts {#shortcuts}", "",
             "[]{.ix t=\"Keyboard shortcuts!list\"}Every command in PHI, "
             f"{total} of them, with the keys it has when PHI is first installed. "
             "Commands without keys can still be found in the command palette (`Ctrl+Shift+P`), "
             "and any of them can be given keys in Options > Keyboard Shortcuts (`Ctrl+Shift+K`); "
             "see [Changing shortcuts](#change-shortcuts).", "",
             "Commands are grouped as the command palette groups them, which is mostly by menu. "
             "Where a command has more than one set of keys, they are separated by a slash.", "",
             "::: note", "The few `Super` keys are the desktop's standard keys for closing and full screen, "
             "which Qt adds by itself. On Hyprland and Omarchy the desktop keeps `Super` for its own use, "
             "so use the other keys listed, or give the command keys of your own.", ":::", ""]
    for category in categories:
        lines += [f"## {category}", "", "| Command | Keys |", "|---|---|"]
        for name, keys in sorted(groups[category], key=lambda r: r[0].lower()):
            lines.append(f"| {name.replace('|', '\\|')} | {keys} |")
        lines.append("")
    (HERE / "chapters" / "shortcuts.md").write_text("\n".join(lines))
    print(f"shortcuts.md: {total} commands in {len(categories)} groups")


if __name__ == "__main__":
    main()
