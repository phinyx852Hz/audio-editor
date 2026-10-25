# Tempo, Warping and Quantize

The Warp menu gives a file a tempo and a beat grid, then lets you stretch it, move its hits and quantize it, the way Ableton Live and Cubase do with audio. PHI has no MIDI editor, so timing is fixed on the audio itself.

## The beat grid {#beat-grid}

[]{.ix t="Beat grid"}[]{.ix t="Tempo"}A file needs a tempo first.

- **Detect Beat Grid** (Warp menu) finds the beats and bars with the QM bar and beat tracker, fits a steady tempo, and moves the grid onto the hits, with bar 1 on the beat with the most low-end punch.
- **Set Tempo and Grid** (`Ctrl+Shift+J`) types the tempo and where bar 1 is.
- **Bar 1 Starts at the Cursor** (`Ctrl+Alt+J`) moves the downbeat to the cursor.
- **Halve Grid Tempo** and **Double Grid Tempo** only change how the beats are counted; the audio is not touched.

Bar, beat and sixteenth lines then show over the waveform with bar numbers. `Ctrl+J` shows or hides them. The BPM box in the transport bar shows the tempo; type a new one there and press `Enter`.

![The beat grid over a loop, with the ruler in bars and beats.](images/beat-grid.png)

[]{.ix t="Loops!automatic tempo"}**Files up to a minute long with no tempo get one as they open**, as in Acid and Live (Warp > Detect Tempo When Opening Loops). Short, busy jungle and drum and bass breaks are read from the loop's length and hits, since a loop is a whole number of bars. The tempo is saved as the file's BPM tag.

### The ruler

[]{.ix t="Ruler"}The time ruler counts time, bars and beats (1, 1.2, 1.2.3), or both on two rows: Warp or View > Ruler in Time, in Bars and Beats, or in Time and Beats.

### Editing on the grid

With **Snap to Grid** (`Ctrl+4`), clicks and selections land on the finest grid line the zoom shows; `Ctrl+1` and `Ctrl+2` narrow or widen it. See [Selecting and Editing](#editing) for moving, duplicating (`Ctrl+D`) and splitting (`Ctrl+E`) on the grid.

## Stretching

[]{.ix t="Stretch to Tempo"}[]{.ix t="Time stretching"}**Stretch to Tempo** (`Ctrl+Alt+Shift+J`) changes the selection, or the whole file, to a new BPM, or fits it to exactly N bars. Every channel is stretched together.

- With **Rubber Band**, the pitch stays where it was. Choose the kind of material: Full Mix, Full Mix + Voice, Drums or Melody.
- With **Varispeed**, speed and pitch change together, like tape.

**Twice as long and half as long.** `*` stretches the selection or the whole file to twice its length and `/` shrinks it to half, pitch kept (also `Ctrl+Alt+Down` and `Ctrl+Alt+Up`). Pressing again goes on (4×, 8×…) as one clean stretch from the original.

**Drag to stretch.** Hold `Ctrl+Alt` and drag the end of a selection to stretch or shrink it to where you let go. The percentage shows while you drag, and the rest of the file moves up or back.

## Warp markers {#warp-markers}

[]{.ix t="Warp markers"}[]{.ix t="Hitpoints"}**Edit Warp Markers** (`Shift+W`) finds the hits (the transients) and shows them as grey ticks along the top of the waveform.

![Warp markers: hits as ticks, markers as flags.](images/warp-markers.png)

- **Drag a tick** to move that hit. It snaps to sixteenths (hold `Alt` to move it freely), and only the audio between its neighbours stretches, so the flag (the warp marker) stays put. Undo puts it back.
- **Shift+drag a flag** to move the flag without moving the audio.
- **Double-click** a tick to add a flag, or a flag to remove it. `W` adds or removes a marker at the cursor.
- `,` and `.` jump to the previous or next hit.
- `Ctrl+Alt+Left` and `Ctrl+Alt+Right` nudge the marker at the cursor by a sixteenth; add `Shift` for 1 ms.
- Double-click between hits to select that slice.

## Quantize {#quantize}

[]{.ix t="Quantize"}**Quantize** (`Q`) moves the hits in the selection, or the whole file, onto the grid, with the Quantize panel's settings. Every quantize renders again from the original audio, so nothing is ever stretched twice, and `Ctrl+Z` undoes it. **Reset Warp** removes the warp markers and puts the timing back as recorded.

## The Quantize panel {#quantize-panel}

[]{.ix t="Quantize panel"}Warp > Quantize Panel (`Shift+Q`) brings Cubase's Hitpoints and Quantize Panel and Live's warping together. Like the other panels it docks at the side or pops out into its own window.

![The Quantize panel, with a bar of the grid and the hits folded onto it.](images/quantize-panel.png)

### Hitpoints

| Setting | Does |
|---|---|
| Detect | **Level** finds sharp rises in loudness (drums, plucks); **Spectral** finds new energy anywhere in the spectrum, so it also finds notes that change without getting louder (legato bass, vocals, chord changes); **Combined** keeps both |
| Threshold | Higher finds quieter hits; the ticks update as you move it |
| Min gap | Ignores hits closer than this to the one before (flams, rolls) |
| Keep | All hits, or only those near 1/4, 1/8 or 1/16 lines, to quantize just the main beats of a busy part |

**Create Warp Markers** flags every hit where it is. **Create Slices** puts a slice marker at every hit, for the Slicer or Export Slices at Markers. **Create Groove** measures the hits' timing against the grid and saves it as a groove you can quantize other audio to.

### Quantize settings

| Setting | Does |
|---|---|
| Grid | 1/1 to 1/64; straight, triplet or dotted |
| Groove | None; built-in grooves (MPC-style swing from 54 to 66 % on eighths and sixteenths, triplet shuffle, pushed offbeats, laid-back backbeat); or grooves you made. A groove replaces the grid and swing |
| Strength | How far each hit moves to its line: 100 % for loops, 50 to 80 % to tighten a performance and keep its feel |
| Swing | Every second step later, up to a third of a step |
| Catch range | Only hits this close to a line move |
| Non-Q zone | Hits already this close stay as played |
| Randomize | Each moved hit lands up to this far either side of its line; **New** rolls new offsets |
| Iterative | Quantizes from where the hits are now, so each press moves them further |
| Auto apply | Changing a setting quantizes again, replacing the last quantize |

The picture under the groove shows one bar: the grid lines, the catch range (shaded) and the non-quantize zone (darker), with the selection's hits folded onto it.

### Warp algorithms

[]{.ix t="Warp algorithms"}

| Algorithm | Best for |
|---|---|
| Full Mix | Whole songs and mixed material |
| Full Mix + Voice | Full Mix that keeps formants, for vocals |
| Drums | Drum loops: short windows keep hits sharp |
| Beats | No stretching at all: cut at every hit, each piece starts at its new place, gaps filled by looping the piece's tail |
| Beats, No Loop | As Beats, but a piece moved apart rings out and fades |
| Melody | Single voices, bass, lead instruments |
| Texture | Pads, drones, noise and ambience |
| Smooth | Long, sustained sounds |
| Varispeed | Speed and pitch together, like tape |

Changing the algorithm renders again what is already warped.

## Export Loop {#export-loop}

[]{.ix t="Export Loop"}[]{.ix t="Loops!exporting"}Warp > Export Loop (`Ctrl+Alt+L`) saves whole bars as a WAV loop. Choose the start bar and how many bars; the seam is crossfaded so it loops without a click. The file carries its tempo, its length in beats and its loop points, so samplers and DAWs (Acid, Live, FL Studio, hardware samplers) loop it in time.
