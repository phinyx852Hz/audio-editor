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
  - **the bols on PHI tabla** (the default): the owner's own strokes (below). Each use of a bol
    plays its next take. The score's track gets PHI tabla, and PHI tabla plays it while it is
    edited.
  - **the bols on General MIDI drums**: each item in turn on a tablā part, a section per item, laid out
    by BP's notation. Fields between periods take a beat each, `/6` and `*2` set the speed, a
    `{a, b}` expression fits its other fields to the first one, `-` is a rest, and `_` lengthens
    the stroke before it. dha is na with ge, dhin is tin with ge, and so on (`strokes()` in
    mixer/BolProcessor.cpp), on the high and low congas, timbale and rim.
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

## PHI tabla

The owner's own recorded strokes (`share/phi-tabla`, 8.6 MB) as an instrument: **PHI tabla** in a
track's instrument list (Built-in). It is a Slicer kit of 31 pads in banks A and B, so Push plays
it on its grid and the Slicer window edits it (`Source/sampler/PhiTabla.h`).

| Pads | What |
|---|---|
| 1–8 (A) | dha, dha 2, dhin, dhin 2, dhi, dhi 2, dhun, kran: compounds, the dāyāṅ stroke and ge (or ke) layered |
| 9–16 (A) | tā, tā 2, tin, tin 2 (sur), te, te 2, te 3, tu |
| 17–24 (B) | tu 2, ge, ge 2, ge (mīṇḍ), ke (kath), ke 2 (kath), tak, trika |
| 25–29 (B) | phrases: dhātete (bharī), tātete (khālī), tirakiṭe roll, gege ×3, ge modulations |
| 30–31 (B) | Sa: the dāyāṅ's (C♯) and the bāyāṅ's (F♯) |

The dāyāṅ's strokes stop each other, and so do the bāyāṅ's, as on the drum. The recordings have
one level each, so softer hits are quieter and also darker: the Slicer's new **velocity tone**
(`Kit::velocityTone`, 0.5 for PHI tabla) closes a low-pass by up to six octaves as velocity falls.
Where there are two or three takes of a stroke, the Bol Processor plays them in turn.

Still to do: the owner's 32-pad Ableton rack layout, if it differs; and slicing the phrases into
strokes with the Slicer, as more takes.
