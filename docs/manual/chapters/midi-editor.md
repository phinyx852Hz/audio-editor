# The MIDI Editor {#midi-editor}

PHI's MIDI editor is a piano roll and a microtonal staff in one, after Cubase's Key Editor. Its rows can be any tuning PHI knows, from 12-TET to a rāga, a maqām, a gamelan laras or a Scala scale, so a melody is written in the system it belongs to. It lives in two places: under the waveform in the main window, where it is a sketchpad with an instrument of its own, and over any MIDI clip in the mixer.

## In the main window {#main-midi-editor}

[]{.ix t="MIDI editor!main window"}[]{.ix t="Audio Editor!closing"}The main window has two halves, in a split you can drag: the audio editor (the waveform with its overview and ruler) above, and the MIDI editor below. Each can be closed, so you can work on audio alone or on MIDI alone:

| Command | Keys | Does |
|---|---|---|
| View > Audio Editor | `Shift+F2` | Closes or opens the top half |
| View > MIDI Editor | `Shift+F4` | Closes or opens the bottom half (or the ✕ at the right of its bar) |

Closing one while the other is closed opens the other, so the window is never empty. PHI starts with them as you left them.

The MIDI editor here belongs to no track: it is one sound being designed, separate from the mixer's project. Its bar holds:

- **Instrument**: the built-in Synth, or any instrument plugin PHI found, the stock instruments included. **Edit** opens the plugin's window. The MIDI keyboard plays it, and with **Step input** on, the keys you play go into the editor. PHI remembers the choice.
- **New**, **Open...** and **Save...**, for standard MIDI files (`.mid`). New and Open ask first if the notes have changed since they were last opened or saved.
- **Bounce to Audio**: plays the notes through a copy of the instrument, as it is set now, from the start to the end of the last note and on until its tail dies away (below −80 dB, at most 10 seconds more). The sound opens in a new tab of the audio editor, unsaved, so nothing already open changes. If the audio editor was closed, it opens.
- **Drop audio as: Melody / Drums**: what an audio file dropped on the MIDI editor becomes (see below).

### Audio to MIDI {#audio-to-midi}

[]{.ix t="Audio to MIDI"}[]{.ix t="Drums to MIDI"}[]{.ix t="Melody to MIDI"}Drag an audio file from the Find panel or the file manager onto the MIDI editor and it becomes notes at once, put in at the step cursor (so a second drop carries on after the first) and selected. `Ctrl+Z` takes them out again.

- **Melody**: one voice at a time, such as a singer, a flute, a bass line or a lead. Its pitch is followed as Hum or Sing hears it, each note set on the nearest degree of the tuning in force, with its slides and shakes kept as pitch curves.
- **Drums**: each hit of a drum loop becomes a kick (36), snare (38) or closed hat (42) on channel 10, chosen by where its energy lies, at the loudness it was played.

A chord comes out as its loudest line; chords are not heard as chords yet.

## On a mixer clip {#midi-editor-clips}

[]{.ix t="MIDI editor!mixer clips"}Pick a MIDI clip in the mixer, on the tracks or in the launcher, then open it with Mixer > Edit MIDI Notes (`Alt+Shift+N`), a double-click on the clip's picture, or **Edit Notes...** in the Inspector. It opens in a window of its own. **Apply to the Clip** writes the notes as a new MIDI file beside the mix, and `Ctrl+Z` on the tracks undoes it.

Compose > New Microtonal Score opens the same editor on four empty bars, on PHI's microtonal staff; see [Composing](#compose).

## Writing notes

[]{.ix t="MIDI editor!tools"}Six tools, picked from the bar or with the number keys:

| Key | Tool | Does |
|---|---|---|
| `1` | **Pick** | Click, `Shift`-click or drag a box to pick notes; drag a note to move it, its right edge to stretch it (`Alt` moves by the cent). `Ctrl`-click or double-click draws a note |
| `2` | **Draw** | Click to put a note, drag to its length (`Alt` puts it between the degrees) |
| `3` | **Erase** | Click or drag over notes to delete them |
| `4` | **Split** | Cuts a note in two where you click |
| `5` | **Glue** | Joins a note to the next of its pitch |
| `6` | **Curve** | Drag across a note to draw its pitch curve (see [Pitch curves](#midi-curves)) |

Right-click deletes a note. **Snap** and the grid (1/1 to 1/32, and triplets) set where notes land, **Length** the length of new ones, and **Quantise** (`Q`) pulls the picked notes towards the grid, with a strength and swing.

| Keys | Does |
|---|---|
| `Up` / `Down` | Moves the picked notes a degree of the system (`Shift`: a period, `Alt`: a cent) |
| `Left` / `Right` | Moves them a grid line (`Alt`: a tick) |
| `Delete` | Deletes them |
| `Ctrl+A` | Picks every note |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo and redo |
| `Space` | Plays from the playhead (click the ruler to move it) |
| `Ctrl+Shift+N` | Switches between the piano roll and the staff |

The info line shows the picked notes' place (bar.beat.tick), name, degree, how many cents they are off it, and their frequency; type into it to set their length, cents and velocity. The **Lane** under the notes draws velocity, or a controller (modulation, volume, pan, expression, sustain, timbre, pressure or pitch bend) on a channel; right-drag clears a controller. The keyboard on the left plays a row when clicked.

**Functions** holds the rest: quantise starts, ends and lengths; legato; fixed lengths; delete overlaps; glue; split at the cursor; velocity (set, compress, expand, add, ramp); up or down a degree or a period; snap to the system's degrees; restyle; and tuning changes.

### Step input {#step-input}

[]{.ix t="Step input"}With **Step input** on, notes go in at the red cursor, which then moves on by the Length. Play them on the MIDI keyboard (keys held together make a chord), click the keyboard on the left, or use the computer's keys: `A` `S` `D` `F` `G` `H` `J` `K` `L` `;` `'` are the degrees up from the tonic in the **period** set, `Q` `W` `E` `R`… the period above, and `Z` `X` `C` `V`… the one below. `Space` is a rest, `Backspace` steps back and deletes what is there. **Chord** (or holding `Shift`) keeps the cursor still; **Insert** moves the notes after the cursor on to make room.

**Hum or Sing...** puts in a melody you sing, hum, whistle or play into the audio input, or one from a recording, at the step cursor, in the system in force. Its options put the notes on the system's degrees, keep slides and shakes as pitch curves, and quantise them.

## Tunings: the rows are the system {#midi-systems}

[]{.ix t="MIDI editor!tunings"}[]{.ix t="Microtonal"}The editor opens in 12-TET, or in the tuning in force on the clip.

- **System** picks any of PHI's tunings (church modes and hypo-modes, jazz modes, rāgas, maqāms and dastgāhs, gamelan laras and pathet, Shona mbira tunings, Chinese, Japanese and Korean modes, 2 to 21 equal steps) **on** a tonic. **Scala** opens a `.scl` scale or `.kbm` map, or saves the system as both. The rows become the system's degrees, a row a degree: seven an octave for rāst, five for sléndro.
- **Rows: By pitch** draws a height for every cent instead, with the system's degrees as lines and the twelve keys dotted behind, so you see how far each degree sits from the piano.
- **Names** writes the rows as the tradition does: sargam for rāgas, kepatihan numbers for gamelan, the Chinese and Korean degree names, maqām letters with quarter-tone signs, or letters with cents, degree numbers or cents.
- A note keeps its pitch when the system changes; one between the rows shows how many cents it is off. **Functions > Snap to the System's Degrees** moves notes onto them.
- **Functions > Tuning Changes > Change the System at the Cursor...** lets a clip change system part way, a modulation from rāst to bayātī or sléndro to pélog. The change is flagged in the ruler, and the rows follow the stretch the cursor is in.
- **Functions > Restyle into This System...** moves a melody to the same degrees of another system, counted from each tonic, so a C major scale into sléndro climbs 1 2 3 5 6. The old notes stay as dashed **Ghosts**, and **Original** plays the clip as it was, to compare.

### Pitch curves and ornaments {#midi-curves}

[]{.ix t="Pitch curves"}[]{.ix t="Ornaments"}A note can carry a pitch curve, drawn in orange over it. Draw one with the **Curve** tool (`6`); right-click clears it. **Functions > Ornaments** writes them for you on the picked notes: mīṇḍ, glide, slide in, kaṇ, āndolan, gamak, vibrato, murkī, khaṭkā and zamzama, or **Clear the Curves**.

### The views

- **View** switches between the **Piano roll**, the **Rising spectrum** (frequency across, time down, each note with its harmonics, so a chord shows as the partials it makes) and **Staff notation**.
- **Wheel** shows the period as a circle with each degree at its true angle against the twelve keys, the notes sounding joined with their intervals in cents, and how rough they sound together.
- **Staff** (`Ctrl+Shift+N`) puts the notes on PHI's microtonal staff, spelled for the system in force: half-flats for maqāms, koron and sori for dastgāhs, Turkish makam signs, Sagittal or ups and downs for equal divisions, HEJI for just tunings. **Notation** picks another: each tradition's own (Bhatkhande sargam, Carnatic swaras, jianpu, kepatihan, jeongganbo), or any of Xen Tuner's notations. On the staff a click picks a note, a double-click writes one, and `Ctrl+Up` and `Ctrl+Down` move notes to the next written pitch.

## Saving: MPE, pitch bend or keys

[]{.ix t="MPE"}A note's pitch is kept in cents, so it can be anywhere. The **Save as** box chooses how the notes are written into the MIDI file, and so how a synth will hear them:

- **MPE**: each note on a channel of its own, bent to its exact pitch. Every system plays exactly on any MPE synth; the built-in Synth and PHI rāga follow it.
- **Pitch bend by channel**: notes bent the same share a channel, for synths without MPE.
- **Keys**: each note on its nearest key, no bends; the track's tuning decides the pitch.

Mode > tuning > send the tuning to mts-esp synths makes PHI the MTS-ESP tuning master, so Surge XT, Vital, Pianoteq and other synths that follow it play in the track's tuning.
