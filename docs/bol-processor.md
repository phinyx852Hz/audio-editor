# Bol Processor

Status: built 2026-10-31. **Compose > South and West Asia > Bol Processor...** runs the Bol
Processor BP3 inside PHI.

## What the owner asked for (2026-10-31)

"I would definitely want the full version if it's available and can be licensed with credit given
to Bel/Kippen." Later: "It would be incredible to connect the slicer and this particular set of
tabla sounds (of my own playing) and the bol processor."

## What it is

The Bol Processor is Bernard Bel and Jim Kippen's grammar engine. They began it in the early 1980s
to model the improvisation of tablā players (Kippen and Bel, "Modelling music with grammars", 1992).
A BP grammar is a set of weighted rewriting rules in subgrammars. It produces items, such as a
qāʿida's theme and variations, written in BP's polymetric notation.

PHI builds the BP3 console from `third_party/bolprocessor` as a program of its own, `phi-bp3`
(about 1 MB). The source is BSD-licensed, by Bernard Bel, Jim Kippen, Srikumar Karaikudi Subramanian
and Anthony Kozar. The full license and credits are installed with PHI.

## The dialog

- **Grammar**: a grammar file (`-gr.name` or `name.bpgr`). The alphabet, settings, sound-object,
  tonality and Csound files it names in its header are found beside it and passed to BP3. It opens
  on PHI's example (below).
- **Seed**: the same seed produces the same items again. 0 means a new seed each time.
- **Produce**: BP3 derives the items, and the dialog shows them as BP3 writes them.
- **Play**:
  - **the bols on the tablā**: each item in turn on a tablā part, a section per item, laid out
    by BP's notation. Fields between periods take a beat each, `/6` and `*2` set the speed, a
    `{a, b}` expression fits its other fields to the first one, `-` is a rest, and `_` lengthens
    the stroke before it. dha is na with ge, dhin is tin with ge, and so on (`strokes()` in
    mixer/BolProcessor.cpp). Until PHI tabla exists, the strokes are General MIDI drums.
  - **BP's own MIDI file**, for a grammar that makes notes rather than bols.
- **Open the Score** opens the result in the MIDI editor.

## The example

`share/bolprocessor` holds the grammar, alphabet and settings of an Ajrada gharānā qāʿida,
"dhin--dhagena". Kippen and Bel wrote this grammar in BP1, with rule weights inferred from real
performances, and Bel carried it over to BP3. PHI's owner learned the qāʿida from Ustad Akram Khan,
a direct descendant of that lineage, and included it on that authority. Each item is 32 beats: the
theme or a variation, bharī and then khālī. The alphabet's `*` homomorphism makes the khālī
(dha→ta, ge→ke).

## Built in

- `third_party/bolprocessor/source`: BP3's C source, unchanged, from
  github.com/bolprocessor/bolprocessor, branch `graphics-for-BP3`, commit 16642df (version 3.5.6).
- `third_party/bolprocessor/curl-shim`: stubs for libcurl. BP3 uses libcurl only to send notes to
  its PHP interface, which PHI does not have.
- `third_party/bolprocessor/console_strings.json`: BP3's message strings, from its PHP interface
  (github.com/bolprocessor/php-frontend, GPL-3.0, by Bernard Bel). BP3 reads it from the folder it
  runs in.
- `cmake/BolProcessor.cmake` builds `phi-bp3` (with ALSA, for BP3's real-time MIDI, which PHI does
  not use).
- `Source/mixer/BolProcessor.h`: runs it and reads its items back. `Source/ui/BolProcessorDialog.cpp`
  is the dialog.

## Next

PHI tabla: the owner's own recorded strokes as a drum instrument, a sample a bol, so the items play
on them. Then the slicer, to cut long takes into strokes and tag each with its bol.
