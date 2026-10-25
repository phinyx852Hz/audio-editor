# The PHI manual

The operation manual for PHI, as a website and a PDF, built from the Markdown chapters in `chapters/`.

```
pip install weasyprint beautifulsoup4    # and pandoc 3, EB Garamond, Inter, JetBrains Mono
python3 build.py            # both
python3 build.py --site     # out/site/index.html
python3 build.py --pdf      # out/PHI-Manual.pdf
```

- `chapters/*.md`: one file per chapter; the order and parts are in `build.py`.
  Index entries are `[]{.ix t="Term!Subterm"}`; call-outs are `::: note`, `tip`, `important`,
  `phi`, `hardware`, `steps`, `formula`, `epigraph`.
- `chapters/shortcuts.md` is generated: `python3 tools/gen_shortcuts.py` from `data/commands.tsv`,
  which a PHI build dumps (every command, its category and its default keys).
- `images/`: screenshots of PHI. A picture that is missing becomes a "screenshot to come"
  placeholder and is listed in `out/missing-screenshots.txt`.
- `images/phi/`: the figures of the Φ chapter, drawn by `tools/phi_figures.py`.
- `tools/make_demo_audio.py`: the demo song, stems, voice, loop, 7.1 and Ambisonics files the
  screenshots use. `tools/shots/`: the helpers that drive PHI under Xvfb for screenshots.
