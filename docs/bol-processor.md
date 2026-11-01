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

- **Grammar**: a grammar file (`-gr.name` or `name.bpgr`), or a data file of items (`-da.name`). The alphabet, settings, sound-object,
  tonality and Csound files it names in its header are found beside it and passed to BP3. It opens
  on PHI's example (below).
- **Seed**: the same seed produces the same items again. 0 means a new seed each time.
- **Produce**: BP3 derives the items, and the dialog shows them as BP3 writes them.
- **Play**:
  - **the bols on PHI tabla** (the default): the owner's own strokes (below). Each use of a bol
    plays its next take. The score's track gets PHI tabla, and PHI tabla plays it while it is
    edited.
  - **the strokes on PHI frame drum**: dum, tak, ka, pa, snap and sweep on the owner's own frame
    drum, each stroke's takes in turn (docs/phi-frame-drum.md). `-da.iqaat` holds the iqāʿāt.
  - **the bols on General MIDI drums**: each item in turn on a tablā part, a section per item, laid out
    by BP's notation. Fields between periods take a beat each, `/6` and `*2` set the speed, a
    `{a, b}` expression fits its other fields to the first one, `-` is a rest, and `_` lengthens
    the stroke before it. dha is na with ge, dhin is tin with ge, and so on (`strokes()` in
    mixer/BolProcessor.cpp), on the high and low congas, timbale and rim.
  - **BP's own MIDI file**, for a grammar that makes notes rather than bols.
- **Open the Score** opens the result in the MIDI editor.

## The examples

`share/bolprocessor` holds Kippen and Bel's tablā and kathak examples from Bel's BP3 test set
(the owner chose "Tabla and kathak grammars only", 2026-10-31); its README lists them.

- Grammars: the Ajrada gharānā qāʿida "dhin--dhagena" (the default), "dhatidhagena" (the one
  Kippen and Bel's 1992 chapter comments), "dhadhatite", and a kathak tihāī on the counting
  syllables (it makes notes, so it plays as BP's own MIDI file).
- Data files (`-da.name`): items already written, Pandit Birju Maharaj's kathak material among
  them. The dialog reads their items as they are, without BP3.

Kippen and Bel wrote the dhin--dhagena grammar in BP1, with rule weights inferred from real
performances, and Bel carried it over to BP3. PHI's owner learned the qāʿida from Ustad Akram Khan,
a direct descendant of that lineage, and included it on that authority. Each item is 32 beats: the
theme or a variation, bharī and then khālī. The alphabet's `*` homomorphism makes the khālī
(dha→ta, ge→ke).

BP's alphabets let bols be written together ("dhatidhage", "dhin--dhagena"): the dialog splits
such a word into its bols, a unit each, as BP does. A BP2 header's homomorphism file (`-ho.name`)
is read as the alphabet, and BP2 settings files, which BP3 cannot read, are left out. **Save BP's
MIDI...** saves BP's own MIDI file, to play on another synth: Bel's sound-objects for dhatidhagena
and the tihāī were made on a Roland D-50.

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

The owner's own recorded strokes (`share/phi-tabla`, 32 files, 11 MB) as an instrument: **PHI
tabla** in a track's instrument list (Built-in). It is a Slicer kit, so Push plays it on its grid
and the Slicer window edits it (`Source/sampler/PhiTabla.h`).

- **Banks A and B** are the owner's Ableton Drum Rack ("TablaDrumRackV.2", 2026-10-31), read from
  its `.als`. Its 32 pads, notes 61 to 92, are in order on pads 1 to 32, with the rack's names,
  sample start and end points, and levels. They run from the phrases (ta-titrakitetaka, dhātete,
  tātete, gege, dhāgenā and others) through kath, te, tin, tā, tak, tu and ge to dhā.
- **Bank C** holds compounds layered from two strokes (dha, dhin, dhi, dhun and kran, two takes
  each where there are two), and the Sa of the dāyāṅ (C♯) and the bāyāṅ (F♯).

The dāyāṅ's strokes stop each other, and so do the bāyāṅ's, as on the drum. The recordings have
one level each, so softer hits are quieter and also darker: the Slicer's new **velocity tone**
(`Kit::velocityTone`, 0.5 for PHI tabla) closes a low-pass by up to six octaves as velocity falls.
Where there are two or three takes of a stroke, the Bol Processor plays them in turn.

Still to do: slicing the phrases into strokes with the Slicer, as more takes.

PHI tabla can be changed after it is made (owner, 2026-10-31: "I would like to be able to tune
the sounds ... after the instrument is made"). In the track's Slicer window each sound has Tune
(semitones, to the cent), level, start and end and fades, and each pad its pitch, pan, envelope
and choke group; the edits are saved with the project. **Kit > Make This the PHI tabla Default**
saves the kit, as edited, where every new PHI tabla (and the Bol Processor's) starts from it
(`PHI tabla.phikit` in PHI's data folder); **Restore PHI tabla's Built-in Default** goes back.
