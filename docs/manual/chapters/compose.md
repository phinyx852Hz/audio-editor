# Composing: the Compose, Mode and Research Menus {#compose}

Three menus make and explain music rather than edit sound. **Compose** writes scores, from a blank page or by a tradition's own methods. **Mode** chooses the mode, harmony, tuning and rhythm everything else works in. **Research** explains the ideas behind them, with sources.

Everything these menus write is MIDI: a score in the [MIDI editor](#midi-editor), clips on the mixer's tracks or in the clip launcher, or a MIDI file. Hear it through the tracks' instruments, then mix and master it as usual.

## The Compose menu {#compose-menu}

[]{.ix t="Compose menu"}From the top:

| Item | Does |
|---|---|
| **New Microtonal Score...** | Opens the MIDI editor on four empty bars, on PHI's microtonal staff, in the Mode menu's tuning. The **Staff** button (`Ctrl+Shift+N`) switches to the piano roll |
| **New Score In** | The same blank score in another notation: Hindustani sargam (Bhatkhande), Carnatic swaras, jianpu, kepatihan, jeongganbo, Arabic maqām, Turkish makam (AEU) or Persian dastgāh staff, or degree numbers |
| **Compose...** | One dialog for every method below |
| **Architectonics** | Song and set structures built on the golden ratio (see below) |
| **Europe and the Americas**, **South and West Asia**, **East and Southeast Asia**, **Africa** | The methods by region, oldest first; each opens the Compose dialog on that method |
| **Rhythm Generator...** | Euclidean timelines against each other, with chance, rules and a feel |

### The Compose dialog {#compose-dialog}

[]{.ix t="Compose dialog"}Choose the **method**, the number of **parts** (up to eight, one for each of the mixer's tracks), roughly how long, the tempo, and the pitches: the Mode menu's mode, or the tradition's own tuning. The dialog shows what the method is and its sources, and after writing lists the sections it made.

- **Deal Again** writes the same form with other notes.
- **Open the Score** opens it in the MIDI editor, a part to a channel, with the sections marked. A score you keep goes onto a track.
- **Save as MIDI...** writes the file.

### The methods

[]{.ix t="Composition methods"}Each method writes a score from nothing, organized as its tradition organizes music.

| Region | Methods |
|---|---|
| Europe and the Americas | Organum, Isorhythm, Species Counterpoint, Fugue, 12-Tone Row, Stochastic Matrix, Stochasticize the Clip, Cubify, Chance and Aleatory, Process Music, Minimalize a Scene, Tintinnabuli, Spectral |
| South and West Asia | Rāga Development, Maqām: Taqsīm and Waṣla, Radif and Dastgāh |
| East and Southeast Asia | Gamelan Colotomic Form, Jo-ha-kyū, Sanjo, Sǎn-màn-zhōng-kuài-sǎn |
| Africa | Layered Ostinato (Ewe), Hocket and Interlocking |

A few work on what you already have in the mixer:

- **Species Counterpoint** and **Fugue** write against the MIDI clip shown in the mixer's Device Chain when there is one (its melody is the cantus or the subject). With none, they write a cantus or subject of their own and open the Compose dialog.
- **Stochasticize the Clip** needs a MIDI clip picked in the mixer; it draws a new line beside it by Xenakis's clouds, sieves and Markov screens.
- **Cubify** arranges the clip shown, or else the launcher's scene, by Xenakis's cubes.
- **Minimalize a Scene** arranges the launcher's scene at the cursor by Reich's and Glass's processes. It is also on a scene's and the timeline's right-click menus.

When a method needs more free tracks than the mixer has, PHI offers to go to [sixteen tracks](#mixer-sixteen).

### Architectonics {#architectonics}

[]{.ix t="Architectonics"}Compose > Architectonics designs a song or a live set by the golden ratio, drawing on the Selector's sounds:

- **Compose a Song...** writes a song into the clip launcher's first empty scenes, a scene to a section.
- **Compose a Score...** writes the same composition from the first bar to the last, for notation.
- **Design a Set...** shows a plan for the launcher's scenes as you change it: each section as a block as wide as its bars and as tall as its energy, with the golden section marked. **Deal Again** gives a new seed.
- **Play the Set** launches each section's scene in turn; choose it again to stop following the set.
- **Deal a New Set** keeps the design with a new seed; while a set plays, the new one starts on the next bar.
- **Endless: a New Set at the End** deals a new set each time one ends, until you stop.

## The Mode menu {#mode-menu}

[]{.ix t="Mode menu"}The Mode menu sets the mode everything else in PHI composes and tunes in. Its names are in lower case and in each tradition's transliteration. It has four parts:

- **melody**: every mode, scale and rāga, by region and then by tradition and person, with **find a mode or tuning...** to search them. Below are the **final** (the tonic, or Sa) and the **range** (authentic, or hypo from the fifth or the fourth), then what to do with the mode: **notes into the mode** (moves the MIDI clip shown onto its notes), **counterpoint...**, **fugue...**, **stochasticize...**, **harmonize in the mode...**, **12 tone row...** and **cubify...**.
- **harmony**: ways voices sound together (see below).
- **tuning**: the tunings, by region the same way, and the equal divisions. **the mode's own tuning** or one chosen here is what the PHI synth plays in; **equal tempered** plays the mode on the piano's keys instead. **tune the track** gives the selected mixer track that tuning, and **send the tuning to mts-esp synths** makes other synths follow it.
- **rhythm**: the **rhythm generator...**, then over a hundred rhythms by region (North Indian tāls, South Indian tāḷams, breakbeats, the medieval rhythmic modes, Caribbean, South American, African, West Asian and Korean rhythms) and **my breaks**, your own drum loops. Picking one adds it to the selected track as a MIDI clip, writes it into a Tracker pattern, or saves it as a MIDI file.

### Harmony {#harmony}

[]{.ix t="Harmony menu"}Mode > harmony maps the many ways of combining voices, not only the Western one:

- **Western art music**, by era, from organum to spectralism;
- **jazz**, a submenu of its own;
- **the world**, by region: drones, parallel and diaphonic singing, heterophony, interlocking and hocket, overtone singing, stratified polyphony (Georgian, Sardinian and Albanian singing, Central African polyphony, gamelan layering, xöömei and more);
- **grown from meeting Western harmony**: shape-note singing, Black American choral harmony, barbershop, isicathamiya, highlife, bossa nova and others.

Choosing one opens a dialog with what it is, where it comes from, the intervals and chords it rests on, and its sources. It writes a short example on the Mode menu's final, as a MIDI clip on the selected track (one channel a voice) or as a MIDI file. A few are heard in their own tuning, and the dialog can tune the track to it. The examples are sketches of the texture, not transcriptions.

**harmonize in the mode...** (under melody) writes an accompaniment in the chosen mode, on its final and in its own tuning, by modal harmony's rules: no V–I, chords moving by step and carrying the mode's characteristic note. Chords are judged by the cents the synth plays, so a maqām's neutral third counts. Each tradition gets its own texture: chords in thirds, chords in fourths for jazz modes, a drone for rāgas, heterophony for maqāms, layers for gamelan, interlocking dyads for mbira.

## The Research menu {#research}

[]{.ix t="Research menu"}The Research menu holds short references on the ideas behind PHI: what each is, its history, and links for further reading. It is not a how-to; that is this manual's job. Its groups:

- **Φ**: the golden ratio, Fibonacci numbers, the golden angle, phase, phase distortion and more;
- **Music theory**: mode, tuning, rāga, maqām;
- **Composition**: counterpoint and fugue, twelve-tone rows, stochastic music, minimalism;
- **Instruments and programs** and **Sound**: the tracker, loudness, Ambisonics, binaural sound, compression, dither;
- **Modes**: every region's modes, rhythms and harmonies, matching the Mode menu.

An entry opens in the research panel on the left, with a search box above it: type any term and press `Enter`. Links to other entries open there; ← goes back, ⧉ pops the panel out into a window, and ✕ or `Esc` closes it. At the bottom of the menu:

- **Search Research...** goes straight to the search box;
- **All References...** shows every entry in one page;
- **Edit References...** copies the reference files into your own folder (`~/.local/share/phi/research`) and opens it. They are Markdown: `# Group` makes a submenu (`Modes / Europe` nests), `## Entry` an item. The menu follows your changes as soon as you save.

::: note
The Research menu shares its letter with Repair: press `Alt+R`, then `R` again, to reach it.
:::
