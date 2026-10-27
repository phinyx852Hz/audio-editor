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

[]{.ix t="Slicer"}The Slicer, after Ninjas2, cuts a sound into slices on 64 pads. `F8` sends the selection, or the whole file, to it; with no file open it opens empty.

![The Slicer: slices over the waveform, and four banks of pads.](images/slicer.png)

**Slicing.** Slice by **transients** (with a sensitivity), **equal parts** (2 to 64), the file's **beat grid** (bars, beats, eighths or sixteenths) or the editor's **markers**. Drag a slice line to move it, double-click to add one, right-click a line to remove it.

**Pads.** The pads come in four banks of 16, A to D, laid out for Push's 8 × 8 grid in User mode: bank A bottom left, B bottom right, C top left, D top right. Or choose **Chromatic**, 16 notes a bank from C1 as in Live's drum racks.

Each pad has its own gain, pan, pitch, reverse, **One shot** or **Gate** play, attack, release and **choke group** (so a closed hi-hat cuts off an open one). The kit has velocity and voices.

**Playing.** Click slices or pads (higher on a pad is louder), play them from Push or any MIDI pads, or from the computer keyboard:

| Keys | Play |
|---|---|
| `Z` `X` `C` `V` | The bank's bottom row of pads |
| `A` `S` `D` `F` | The second row |
| `Q` `W` `E` `R` | The third row |
| `1` `2` `3` `4` | The top row |
| `5` `6` `7` `8` | Choose bank A to D |

A pad hit from MIDI becomes the one you edit. Follow editor, `Ctrl+F8` (refresh) and `Shift+F8` (window) work as for Simplest.

## Both together

- MIDI notes play whichever of the two windows you used last. Closing one silences it; with both closed, MIDI plays nothing.
- `Ctrl+Alt+P` docks one instead of a window: Simplest along the bottom beside the Device Chain, the Slicer on the left under Find. It stays docked until you pop it out again.
- Drag an audio file from the Find panel or a file manager onto either to load it.
- In the Find panel, `F7` and `F8` send the highlighted file and leave the keyboard in Find, so you can arrow to the next file and try it.
- A mixer track can have a Simplest or Slicer of its own; see [Instruments](#mixer-instruments).
