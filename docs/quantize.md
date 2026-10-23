# Audio quantizing

PHINYX has no MIDI editor, so timing is fixed on the audio itself, the way Cubase
(AudioWarp, Hitpoints, the Quantize Panel) and Ableton Live (warp markers, Quantize,
warp modes) do it. Everything lives in the **Quantize panel** (Warp > Quantize Panel,
`Shift+Q`). Like the other panels it docks at the side of the editor or pops out into
its own window (on Hyprland it tiles like any other window).

The file needs a tempo first: **Detect** in the panel (Warp > Detect Beat Grid) or
**Set...** (`Ctrl+Shift+J`).

## Hitpoints

Turn on **Edit Warp Markers** (`Shift+W`). The hits show as ticks along the top of
the waveform, warp markers as flags.

| Setting | What it does |
| --- | --- |
| Detect | **Level** finds sharp rises in loudness (drums, percussion, plucks). **Spectral** finds new energy anywhere in the spectrum, so it also finds notes that change without getting louder (legato bass, vocals, chord changes). **Combined** keeps both. |
| Threshold | Higher finds quieter hits. The ticks update as you move it. |
| Min gap | Hits closer than this to the one before are ignored (flams, rolls, buzzing decays). |
| Keep | All hits, or only those near 1/4, 1/8 or 1/16 lines (Cubase's Beats filter): quantize just the main beats of a busy part. |

**Create Warp Markers** puts a flag on every hit in the selection (or file) where it
is. **Create Slices** puts a slice marker at every hit (Markers > Export Slices, the
Slicer, or `Ctrl+D`/drag to move hits by hand: Cubase's event-start quantizing).
**Create Groove** measures the timing of the hits against the grid and saves it as
a groove template you can quantize other audio to.

## Click and drag

* Drag a tick or flag along the top strip: that point of the audio moves and only
  the audio between its neighbours stretches. It snaps to the panel's grid (hold Alt
  for free placement, or untick *Dragged markers snap to the grid*).
* Shift+drag a flag to move the flag without moving the audio.
* Double-click a tick to add a flag, or a flag to remove it. `W` does the same at the cursor.
* **Ctrl+Alt+drag the end of a selection** to stretch or shrink the selection to
  where you let go (the percentage shows while you drag). The rest of the file moves
  up or back.
* `*` and `/` (or ×2 and ÷2 in the panel) make the selection twice or half as long.

## Quantize

| Setting | What it does |
| --- | --- |
| Grid | 1/1 to 1/64; Straight, Triplet or Dotted. |
| Groove | None, built-in grooves (MPC-style swing at 54 to 66 % on 8ths and 16ths, triplet shuffle, pushed offbeats, laid-back backbeat), or grooves you created. A groove replaces the grid and swing. |
| Strength | How far each hit moves to its line. 100 % for loops; 50 to 80 % tightens a performance and keeps its feel (Live's Amount). |
| Swing | Every second step later, up to a third of a step (a triplet feel). |
| Catch range | Only hits this close to a line move (Cubase's catch range / max move). |
| Non-Q zone | Hits already this close stay as played. |
| Randomize | Each moved hit lands up to this far either side of its line. *New* rolls new offsets. |
| Iterative | Quantize from where the hits are now, so each press moves them further. |
| Auto apply | Changing a setting quantizes again, replacing the last quantize (one undo step). |

The picture under the groove shows one bar: the lines, the catch range (shaded) and
non-quantize zone (darker), with the selection's hits folded onto it as ticks.

**Quantize** (`Q`) works on the selection, or the whole file. **Reset** removes the
warp markers in the selection (or all), putting the timing back as recorded. Every
quantize re-renders from the original audio, so nothing is ever stretched twice, and
`Ctrl+Z` undoes it.

## Warp algorithms

| Algorithm | Best for |
| --- | --- |
| Full Mix | Whole songs and mixed material (Rubber Band, finer engine). |
| Full Mix + Voice | Full Mix that keeps formants, for vocals. |
| Drums | Drum loops: short windows keep hits sharp. |
| Beats | No stretching at all: cut at every hit and each piece starts at its new place; gaps are filled by looping the piece's tail. Hits stay exactly as recorded (Live's Beats mode, Cubase's slice-and-move). |
| Beats, No Loop | As Beats, but a piece moved apart rings out and fades instead of looping. |
| Melody | Single voices, bass, lead instruments. |
| Texture | Overlapping, slightly scattered grains for pads, drones, noise and ambience. |
| Smooth | A soft phase vocoder with long windows for sustained sounds. |
| Varispeed | Speed and pitch change together, like tape (Live's Re-Pitch). |

Changing the algorithm re-renders what is already warped.

All of this is PHINYX's own code, apart from Rubber Band (GPL-2.0-or-later), which
the phase vocoder modes use.
