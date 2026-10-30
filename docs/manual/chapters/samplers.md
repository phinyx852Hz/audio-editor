# Simplest and the Slicer

The Sample menu (`Alt+S`) holds PHI's samplers. Two are built into PHI and work hand in hand with the editor: **Simplest**, which plays a sound across the keyboard, and the **Slicer**, which cuts a sound into pads. (The Sample menu's other samplers, Granular and Retro Sampler, are in [Instruments](#instruments).)

Both open in windows of their own, so they get the room they need whatever panels are open; on Hyprland they tile, and `Super+F` takes one full screen. They sound on top of whatever else plays, file or tracker.

## Simplest {#simplest}

[]{.ix t="Simplest"}[]{.ix t="Sampler"}`F7` sends the selection, or the whole file, to Simplest and plays it across the keyboard. With no file open, `F7` opens Simplest empty, ready for a sound dropped on it.

![Simplest, with the start, end and loop lines over the sound.](images/simplest.png)

**Follow editor.** With Follow editor on, every later edit to that sound in the editor reaches Simplest at once, keeping its start, end and loop. `Ctrl+F7` refreshes by hand; `Shift+F7` shows the window.

**Modes.**

- **Classic** loops the shaded part while a key is held and shapes it with Attack, Decay, Sustain and Release.
- **1-Shot** plays the whole sound, with short fades, whatever the key does.

**Setting it up.** Drag the white **S** and **E** lines to set where it starts and ends, and the **L** lines for the loop; they snap to zero crossings. Set the gain, pan, transpose, detune, root key, voices (1 to 32) and how much velocity matters.

### Playing it

[]{.ix t="Computer keyboard as piano"}From the computer keyboard, as in Live:

| Keys | Play |
|---|---|
| `A` `S` `D` `F` `G` `H` `J` `K` | White keys |
| `W` `E` `T` `Y` `U` | Black keys |
| `Z` / `X` | Octave down / up |
| `C` / `V` | Velocity down / up |
| `Esc` | Back to playing from a field |
| `Shift+Esc` | All notes off |

[]{.ix t="MIDI inputs"}Or from any MIDI keyboard or pad controller: Sample > MIDI Inputs chooses which. The sustain pedal and pitch bend work.

## The Slicer {#slicer}

[]{.ix t="Slicer"}The Slicer is a drum machine of 128 pads, after Ninjas2, Cubase's Drum Machine and Ableton's Drum Rack. It cuts a sound into slices across the pads, and any pad can hold sounds of its own instead. `F8` sends the selection, or the whole file, to it; with no file open it opens empty.

![The Slicer: slices over the waveform, and the pads.](images/slicer.png)

**Slicing.** Slice by **transients** (with a sensitivity), **equal parts** (2 to 128), the file's **beat grid** (bars, beats, eighths or sixteenths) or the editor's **markers**. Drag a slice line to move it, double-click to add one, right-click a line to remove it. Slicing again leaves pads with sounds of their own alone.

**The window** is laid out as Cubase's Drum Machine. On the left are the banks and the pads, in the middle the chosen pad's layers beside its sound, and on the right the pad's **Level**, **Pan** and **Pitch** knobs and its settings. **Instrument** and **Steps** along the top switch the middle between the layers and the step sequencer, and the slicing settings sit beside them.

**Pads.** The pads come in eight banks of 16, A to H, shown as small 4 × 4 grids above the pads (lit while a pad in them sounds). Each pad has its own **M** and **S** to mute and solo it, and its note. They are laid out for Push's 8 × 8 grid in User mode: bank A bottom left, B bottom right, C top left, D top right. **Push plays** puts banks E to H on the grid instead; the four banks off the grid answer the notes left over (100 to 127, then 0 to 35). Or choose **Chromatic**: pad n is MIDI note n, 16 notes a bank, as in Cubase, so C1 is pad C5. Kits from before 128 pads open on the same notes.

An empty pad shows only its note, a pad with one sound its name, and a layered pad **Multi**.

**Sounds of their own.** Drop audio files on a pad (several fill the pads after it), or right-click it and choose **Load Sound**. A pad with its own sound shows that sound in the waveform, with the part it plays shaded.

**Each pad** has its own gain, pan, pitch, **One shot** or **Gate** play, attack, release, **choke group** (1 to 16, so a closed hi-hat cuts off an open one), **Mute**, **Solo** and **Receive**, the MIDI note it answers. Auto is its note in the layout; pads given the same note play together. The kit has velocity and voices (1 to 64; each layer of a hit takes one).

**Layers.** The chosen pad's four layer slots each show what they hold, with a power button and a level knob. Click a slot to edit its tune, start, end, fade in, fade out and reverse below. Each is a slice or a sound, and all of a pad's layers play at once.

**The pad menu.** Right-click a pad to rename it, give it one of 16 colours, mute or solo it, load a sound, cut, copy, paste or reset it, or reset every mute, solo or pad. Drag a pad onto another to swap them; each keeps the note it answers.

**Hot swap.** Under the layer slots, turn on **Hot swap** and send sounds from the Find panel with `F8` (or drop them on the window). Each goes onto the chosen pad's layer to try, keeping that layer's level, tune and fades. **Keep** keeps it; **Undo Swap** puts back what was there.

**Kits.** **Save Kit** writes the whole kit as a `.phikit` file, with the sample in a `Sounds` folder beside it. Pads' own sounds stay where their files are. **Load Kit** opens one. A mixer track's Slicer keeps its kit in the `.phi` project the same way.

**Steps.** []{.ix t="Step sequencer"}**Steps**, along the top, swaps the layers for the step sequencer, after Cubase's Pattern Editor and FL Studio's Channel Rack. Each pad of the bank showing is a row of steps: click a step to turn it on or off and drag along to paint, click a pad's name to choose it, and **M** and **S** mute and solo its lane. A brighter step is louder; a number is how many times it hits, and **?** means it does not hit every time.

- **Lanes.** The chosen pad's lane has its own length (1 to 64 steps), step length (1/4 to 1/64, dotted and triplet), direction (forward, backward, back and forth, or random, which draws the same steps each time round), the step it starts **from**, and its own **swing**. Lanes of different lengths run against each other. The **Lane** menu fills every 1, 2, 4 or 8 steps, shifts the steps left or right, or clears the lane.
- **Drawing.** Under the grid, **Draw** picks one setting of the chosen pad's steps to draw as bars: velocity, gate (how long a Gate pad is held), pitch (±24 semitones), chance, repeats (up to 8 hits within the step), ramp (the repeats getting louder or softer) and shift (early or late, up to half a step).
- **Patterns.** There are 16. Choosing one edits it and plays it next, from the next bar while playing. **Edit** copies, pastes or clears a pattern. **Swing** delays every other step, and **bars** is how long the pattern plays in a chain. A **Chain** plays patterns in turn, by number (`1 1 2 3`).
- **Transport.** **Play** (or `Space` while Steps shows) plays the patterns at their own tempo. **Follow Mixer** plays in time with the Mixer instead: its transport starts and stops them, at its tempo, from its position. With **Record** on, pads you hit while it plays land on the nearest step.

**Save Kit** keeps the patterns with the kit, and **Load Kit** brings them back.

**Playing.** Click slices or pads (higher on a pad is louder), play them from Push or any MIDI pads, or from the computer keyboard:

| Keys | Play |
|---|---|
| `Z` `X` `C` `V` | The bank's bottom row of pads |
| `A` `S` `D` `F` | The second row |
| `Q` `W` `E` `R` | The third row |
| `1` `2` `3` `4` | The top row |
| `5` `6` `7` `8` `9` `0` `-` `=` | Choose bank A to H |

A pad hit from MIDI becomes the one you edit. Follow editor, `Ctrl+F8` (refresh) and `Shift+F8` (window) work as for Simplest.

## Both together

- MIDI notes play whichever of the two windows you used last. Closing one silences it; with both closed, MIDI plays nothing.
- Both dock compactly along the bottom, as Cubase's Drum Machine in its lower zone, and take turns with the Device Chain in the same space: tabs along the top (Device Chain, Simplest, Slicer) switch between them, opening one brings it to the front, and `Shift+Tab` (outside a field you type in) or `Ctrl+Shift+C` brings the Device Chain back. With the mixer in front they sit inside it, under the strips, with the same tabs in place of its Device Chain; Edit on a track's Sampler or Slicer opens it there as well, as another tab. They take only the height their layout needs, about the Device Chain's. `Ctrl+Alt+P` pops one out into its own window and docks it back.
- Drag an audio file from the Find panel or a file manager onto either to load it.
- In the Find panel, `F7` and `F8` send the highlighted file and leave the keyboard in Find, so you can arrow to the next file and try it.
- A mixer track can have a Simplest or Slicer of its own; see [Instruments](#mixer-instruments).
