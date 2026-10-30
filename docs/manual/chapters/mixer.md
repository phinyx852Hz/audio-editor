# The Mixer, Patchbay and Projects

PHI's mixer is a small DAW of eight tracks. Every track is the same kind of track, as in REAPER, Bitwig or Tracktion: a timeline of clips, each an audio file or a MIDI file, plus an instrument that the MIDI clips play. The mixer mixes stems from another DAW, sketches and arranges music, and plays live; the mix then goes into the editor, where the Master Section masters it.

## Opening the mixer

[]{.ix t="Mixer"}Mixer > Mixer (`F3`) puts the mixer in the editor's place, between the side panels. `F3` again or `Ctrl+W` closes it, and the next `F3` brings it back as you left it.

- `Ctrl+Shift+F3` (or `Ctrl+Alt+P` from the mixer) undocks it into a window of its own, and docks it back.
- `Super+F` takes it full screen.
- `Shift+F3` plays and stops the mix from anywhere.

![The mixer, undocked: the file browser on the left, the tracks across time, and below them the strips, the A and B returns and the master.](images/mixer.png)

The window has three sections. `F6` or `Tab` goes round them (`Shift` backwards).

## The file browser

[]{.ix t="Mixer!file browser"}On the left is a file browser (`Ctrl+B` shows or hides it). The arrows walk folders and files; `Backspace` goes up a folder; the ★ keeps a folder among the favourites.

`Enter` or a double-click puts the file on the selected track at the playhead, and moves the playhead to its end, so files can be laid one after another. `Shift+Enter` leaves the playhead where it is.

## Tracks and clips

[]{.ix t="Clips"}[]{.ix t="Tracks"}In the middle the tracks run across time, one row each (`V` shows or hides them). Audio clips show their waveform and MIDI clips their notes; overlapping clips stack within the row. A muted clip, or one silenced by its track's mute or another track's solo, is grey.

**Adding clips.** Drag files from the browser or a file manager onto any spot of a row; several land one after another. Audio clips are any audio file PHI reads; MIDI clips are standard MIDI files (types 0 and 1), played as they are, never edited.

**With the mouse:**

- drag clips to move them in time or to another track (`Ctrl` copies, `Shift` leaves the grid, `G` turns the grid off and on);
- drag a clip's edge to trim it;
- click or drag on empty space to move the playhead;
- `Ctrl` + wheel zooms; double-click the ruler to see the whole mix;
- right-click a clip for its menu.

**From the keyboard**, with the rows focused:

| Keys | Does |
|---|---|
| `Left` / `Right` | Moves the playhead a beat (`Ctrl`: a bar) |
| `Up` / `Down` | Picks the track |
| `Delete` | Removes the selected clips |
| `Ctrl+E` | Splits at the playhead |
| `Ctrl+D` | Duplicates |
| `Shift+M` | Mutes |
| `R` | Reverses: an audio clip plays backwards, a MIDI clip plays its notes in reverse; `R` again turns it back |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undoes and redoes clip edits |

### Editing a clip in the editor

[]{.ix t="Clips!editing"}Double-click an audio clip to edit it in the wave editor. The mixer steps aside but keeps running, and the clip opens in a tab of its own. When you are done, `F3` puts the edit back on the track, closes the tab and brings the mixer back. The edit is saved as a WAV in an `Edits` folder beside the mix (or in `~/Music/PHI Edits` before the mix is saved); `Ctrl+Z` in the mixer undoes it.

## Instruments

[]{.ix t="Instruments!on mixer tracks"}Each track has an instrument that its MIDI clips play, and that the MIDI keyboard plays when the track is armed (`R`, the dot):

- the built-in **Synth**: sine, triangle, saw or square through a low-pass, with General MIDI drums on channel 10, so any MIDI file sounds at once;
- a **Sampler** or **Slicer** of the track's own, with its sound from a file or from the Sample menu; Edit opens the usual window for it;
- any **VST3, LV2 or CLAP instrument** found by the plugin scan, including PHI's own instruments (see [Instruments](#instruments)); Edit opens its window.

## The strips

[]{.ix t="Mixer!strips"}[]{.ix t="Fader"}Below the tracks are the strips: one per track, the two returns, and the master. PHI's strip takes what each of two DAWs does best: the insert slots of Cubase and the simple sends of Ableton Live. Every track's sound goes through the same steps, in this order:

::: formula
trim → polarity → low cut → high cut → inserts → width → pan → fader
:::

From top to bottom, each strip has:

- the **instrument**, **transpose** (the drums on channel 10 stay put) and which **MIDI channels** play;
- the **Pre** section: polarity per side (**ØL** and **ØR**), the **LC** and **HC** cuts, and the track **Delay**;
- the **insert slots**;
- **Trim**, **Pan** and **Width**, three small pots side by side with their values below;
- the **Send A** and **Send B** pots, the track **mode** and **Duck**;
- **mute** and **solo**;
- a **fader**, −∞ to +6 dB, marked in dB on its left: drag it, `Shift` for fine moves, double-click for 0 dB;
- a **meter**, peak and RMS in dBFS on its right, with peak hold and a clip light. It changes colour from −12 dB and again from −6 dB.

![A mixer strip, with Φ EQ and Φ Tame in its inserts.](images/mixer-strip.png)

[]{.ix t="Mixer!Strips button"}The **Strips** button shows or hides the Instrument, Pre, Inserts and Trim/Pan/Width sections on every strip at once, and makes the strips wide. **Only Faders** hides them all.

### Pre: polarity, cuts and delay {#mixer-pre}

[]{.ix t="Low cut"}[]{.ix t="High cut"}[]{.ix t="Track delay"}

- **Trim**, ±24 dB, sets the gain before everything else.
- **ØL** and **ØR** turn the polarity of each side.
- **LC** and **HC** are a low cut and a high cut, from 20 Hz to 20 kHz at 12 or 24 dB an octave, off to start. Click one to turn it on or off; right-click it for its frequency and slope.
- **Delay** moves the whole track up to 500 ms earlier or later, to line it up with the others or for groove. PHI reads the track's clips that much earlier or later, so nothing is held back and a negative delay works too.

### Inserts {#mixer-inserts}

[]{.ix t="Inserts"}[]{.ix t="Mixer!inserts"}Each track has up to eight insert effects. The slots are always shown, as in Cubase: at least four, and one empty slot after the last one used.

- Click an empty slot, or press `Insert`, for the effects menu, sorted by format: Built-in, Φ, stock, VST3, LV2, LADSPA and CLAP.
- Double-click a slot to open the plugin's window, or for a built-in or Φ effect, a window of its knobs.
- Right-click a slot to bypass, move, replace or remove it, or to set its **Mix**.
- Drag a slot to reorder it.

[]{.ix t="Insert Mix"}**Mix** is each insert's own dry/wet, for parallel compression or saturation in one slot. A slot under 100 % shows its percentage.

### Width and pan {#mixer-width}

[]{.ix t="Width"}**Width** runs from 0 % (mono) to 200 %, through mid/side. A track of mono files has none. **Pan** comes after the width: equal power for a track of mono files, balance otherwise.

### Sends and returns {#mixer-sends}

[]{.ix t="Sends"}[]{.ix t="Returns"}Two return strips sit between the tracks and the master:

- **A Reverb** starts with Φ Space (1.8 s, fully wet).
- **B Delay** starts with Φ Echo: one tap, a dotted eighth at the mix's tempo, 30 % feedback, fully wet.

Right-click a return's effect to choose another, or none. Each return has a balance pan, a fader and a mute, and its own meter.

Every track has a **Send A** and a **Send B** pot, showing its level in dB. A send is taken after the fader and pan; right-click the pot for **Pre-Fader**, which takes it after the inserts and width instead. A muted track's sends are silent, and bounces have none.

### Track modes and Duck {#mixer-duck}

[]{.ix t="Track modes"}[]{.ix t="Hybrid track"}A track plays in one of three modes:

| Mode | Plays |
|---|---|
| **Hybrid** (the default) | The audio clips and the instrument as two layers, each with its own level (the **Audio** and **Inst** pots) |
| **Audio** | Only the audio clips |
| **Instrument** | Only the instrument |

With every track hybrid, eight tracks hold sixteen sounds. The layers are added together before the trim and the fader, with room to spare, so nothing clips inside the strip; if a track runs hot, the meter's red shows it and the Trim brings it down.

[]{.ix t="Duck"}[]{.ix t="Sidechain"}The **Duck** pot, 0 to 48 dB, turns one sound down whenever another plays: a sidechain that follows the peaks of its key. At −6 dBFS and above the key turns the ducked sound down by the whole amount, and below that by its share. The key is one layer of a hybrid track (the instrument ducks the audio, or the audio the instrument) or another track, which ducks the whole of this one. Right-click the pot for:

- the key, the attack and the release;
- **Only Below**, which ducks only what lies below a frequency and lets the highs pass, so a drum loop's hats keep going under the kick;
- three presets: **Hard** (24 dB, fast release), **Soft** (8 dB, slow release) and **HF-Safe** (12 dB below 150 Hz).

The pot's label shows the key and the amount: `I→12` is the instrument ducking by 12 dB, and `3→24↓` is track 3 ducking the lows by 24 dB.

### Changing several strips at once {#q-link}

[]{.ix t="Q-Link"}

- **Q-Link**, as in Cubase: `Ctrl`+click strips to pick them, and with Q-Link on, a change to one goes to all of them. Levels move by the same amount.
- Hold `Alt+Shift` while changing a control to send the change to every track.
- **Bypass Inserts** and **Bypass Sends** switch off every insert or every send in the mix at once, to hear it without them.

| Keys (strips focused) | Does |
|---|---|
| `1` to `8` / `9` | Picks a track / the master |
| `Left` / `Right` | Moves between strips |
| `Up` / `Down` | Moves the fader (`Shift`: 0.1 dB) |
| `[` / `]` | Pans |
| `0` | Resets the fader |
| `M` / `S` | Mute / solo |
| `E` | Edits the instrument |
| `A` / `I` | Adds an audio or a MIDI file at the playhead |

A new mix starts with two tracks; Mixer > Add Track (`Ctrl+T`) adds one, up to eight, and Remove Last Track (`Ctrl+Shift+T`) removes the last. The master has a fader and meter and no plugins: mastering stays the editor's job.

All of it, the cuts, delay, width, inserts with their settings, sends, modes and ducking, is saved with the project.

**Transport.** `Space` plays and stops, `Home` goes to the start, `L` loops the whole mix. The tempo box sets the bars and beats; **MIDI follows tempo** plays the MIDI files at it instead of their own tempo maps.

## Mixing down and bouncing {#mixdown}

[]{.ix t="Mix Down to Editor"}[]{.ix t="Bounce"}**Mix Down to Editor** (`Ctrl+F3`, or `Ctrl+Enter` in the mixer) renders the whole mix, with the instruments' tails, into a new stereo tab in the editor, ready for mastering.

**Bounce** renders one track by itself (its MIDI clips through its instrument, plus its audio clips, through the cuts, inserts and width, before the fader and pan) to a 32-bit WAV in a `Bounces` folder beside the mix:

- **Bounce in Place** (`B`, or right-click the strip) puts it on the track and mutes the clips it came from. They stay, so you can go back. The cuts and width are turned off, the inserts are bypassed and the delay is reset, so the sound is not processed twice.
- **Bounce to New Track** (`Shift+B`) puts it on a new track with the same fader and pan, and mutes the original. It needs a free track.

The mixer is heard on top of the editor, like the samplers.

## The Mix Assistant {#mix-assistant}

[]{.ix t="Mix Assistant"}Mixer > Mix Assistant (`Alt+Shift+X`) is like Neutron's Mix Assistant. Like the [Master Assistant](#master-assistant), it only sets what you already have: each track's **fader** and **pan**, and the EQ cuts if you ask, never its clips, instrument, trim or the patchbay.

![The Mix Assistant: each track's role, its setting before and after, and where one track hides another.](images/mix-assistant.png)

**Roles.** Each track gets a role, guessed from its name (Lead Vox, BV, Kick, Snare, DR, BA, Gtr, Keys, Pad, FX… and the short Selector names) and changeable in the list: lead vocal, backing vocal, kick, snare, drums, bass, lead instrument, guitar, keys, pad, effects or other.

**Suggest** bounces every track that is heard (muted and soloed-out tracks are left alone), then fits the faders so each track sits at the loudness its role suggests against the others: the lead on top, kick and bass close behind, pads and effects under them, after the equal-loudness automatic mixers of Perez-Gonzalez and Reiss. The whole mix aims for a loudness you pick (−18 LUFS) with its peaks under a ceiling (−6 dBTP), to leave room for mastering, and, if you pick a reference from the References panel, its tonal balance.

**Pans** follow rules: the lead vocal, kick, snare and bass stay in the middle, as do stereo tracks that are already wide; the rest spread to both sides in turn, the brightest widest. Tracks you panned yourself keep their pans unless you untick **Keep pans I set**. Untick **Fader** or **Pan** on a track to leave it alone.

The list shows each track's setting before and after, its level in the mix against the aim, and below it where one track hides another ("Gtr covers Lead Vox around 1 kHz: cut Gtr there by about 2 dB"). **Apply EQ Cuts** puts those cuts in a [Φ EQ](#phi-eq-nodes) in the inserts of the track that hides the other, each on the nearest band, or you can make them by hand. **Amount**, `A`, `Space`, **Keep** and **Revert** work as in the Master Assistant.

## The Patchbay {#patchbay}

[]{.ix t="Patchbay"}Mixer > Patchbay (`F4`) is an 8 × 8 grid that routes everything PHI plays live. It is the router itself, not a view of it: the mixer's tracks, the samplers, the synths and the editor's playback all go through it.

![The patchbay: sources down the side, destinations across the top.](images/patchbay.png)

| Point | As a source (row) | As a destination (column) |
|---|---|---|
| Editor: the open file, after the Master Section | yes | |
| Sampler and Slicer | yes | |
| Synth 1 to 4: the patchbay's own instruments | yes | |
| Track 1 to 8: out after the fader, in before trim, pan and fader | yes | yes |
| Mix Bus: the mixer's master fader | yes | yes |
| FX Bus 1 to 4: one effect each | yes | yes |
| Main Out: the speakers | | yes |

Out of the box, every source goes where it would go anyway (each track to the Mix Bus, everything else to the Main Out), so nothing changes until you patch.

- **Click a jack** to patch or unpatch. A lit jack takes the source's colour and glows with what goes through it.
- **Drag it** up or down, scroll, or press `+` and `-` for its gain (`Shift`, or `[` and `]`, for 0.1 dB), from off to +12 dB; double-click or `0` for 0 dB.
- The arrow keys move between jacks; `Space` patches.
- **Crossed-out jacks** would feed a signal back into itself, and are refused; the status line says why.
- Click a **row or column name** for its menu: put something else there, choose a synth's instrument or an FX bus's effect and open its editor, or unpatch everything it has.
- **Auto-fill** (on by default) fills the grid with what is in use and keeps up as that changes.

Patches, synths and effects are part of the project and come back with it.

## Projects {#projects}

[]{.ix t="Projects"}[]{.ix t=".phi files"}A project is the mixer's session in one file: the tracks with their clips and instruments (plugin settings included), the strips and master, the patchbay's patches, synths and effects, the clip launcher, and whether the mixer was in front.

| Command | Keys (in the mixer) |
|---|---|
| File > New Project, or Mixer > New Project | `Ctrl+N` |
| Mixer > New Project from Selector Set | (see [From the Selector to a mix](#selector)) |
| Mixer > Open Project | `Ctrl+O` |
| Mixer > Save Project | `Ctrl+S` |
| Mixer > Save Project As | `Ctrl+Shift+S` |

A `.phi` also opens with File > Open, from the command line, or from the file manager. Paths inside are relative to the project, so a folder holding the project and its files can be moved. Sounds taken from the Sample menu are saved as WAVs beside the project.

**PHI starts on a blank project.** Quitting with unsaved changes asks to Save, Discard or Cancel. If PHI did not close properly, the next start offers to recover the project as it was, unsaved changes and all, with the mixer in front if you left it there.
