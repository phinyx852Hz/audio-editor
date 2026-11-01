# The Mixer, Patchbay and Projects

PHI's mixer is a small DAW of eight tracks, or sixteen when you ask for them (see [Sixteen tracks](#mixer-sixteen)). Every track is the same kind of track, as in REAPER, Bitwig or Tracktion: a timeline of clips, each an audio file or a MIDI file, plus an instrument that the MIDI clips play. The mixer mixes stems from another DAW, sketches and arranges music, and plays live; the mix then goes into the editor, where the Master Section masters it.

## Opening the mixer

[]{.ix t="Mixer"}Mixer > Mixer (`F3`) puts the mixer in the editor's place, between the side panels. `F3` again or `Ctrl+W` closes it, and the next `F3` brings it back as you left it.

- `Ctrl+Shift+F3` (or `Ctrl+Alt+P` from the mixer) undocks it into a window of its own, and docks it back.
- `Super+F` takes it full screen.
- `Shift+F3` plays and stops the mix from anywhere.
- `Ctrl+Shift+C` hides or shows the mixer's own [Device Chain](#mixer-device-chain) while the mixer is in front.

![The mixer, undocked: the file browser on the left, the tracks across time, and below them the strips, the A and B returns and the master.](images/mixer.png)

The window has three sections. `F6` goes round them (`Shift+F6` backwards).

## The file browser and the Inspector

[]{.ix t="Mixer!file browser"}[]{.ix t="Inspector"}The left column holds either the file browser or the **Inspector**, at the same width, one in place of the other.

- **The browser** shows first. In a window of its own the mixer has its own browser (`Ctrl+B` shows or hides it); docked in the editor's place, the editor's Find panel plays that part. The arrows walk folders and files; `Backspace` goes up a folder; the ★ keeps a folder among the favourites. Its places end with **Instruments** and **Audio Effects**: drag one onto a strip, the Inspector or the Device Chain, or press `Enter`, to put it on the selected track.
- **The Inspector** shows the selected track's mode, instrument, Pre, inserts, sends and width, and, when a clip is picked, a section for that clip. Clicking a track (its strip, its row, its launcher column or a slot) or picking it with `1` to `8` brings it up; the **Inspector** button, or `Ctrl+F`, switches back and forth.

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

### Editing a clip's notes {#mixer-edit-notes}

[]{.ix t="Clips!MIDI notes"}Pick a MIDI clip, on the tracks or in the launcher, then open it in the [MIDI editor](#midi-editor) any of these ways: Mixer > Edit MIDI Notes (`Alt+Shift+N`), a double-click on the clip's picture in the Device Chain, or **Edit Notes...** in the Inspector's MIDI Clip section. **Apply to the Clip** writes the notes as a new MIDI file beside the mix, and `Ctrl+Z` on the tracks puts the clip back as it was.

### Editing a clip in the editor

[]{.ix t="Clips!editing"}Double-click an audio clip to edit it in the wave editor. The mixer steps aside but keeps running, and the clip opens in a tab of its own. When you are done, `F3` puts the edit back on the track, closes the tab and brings the mixer back. The edit is saved as a WAV in an `Edits` folder beside the mix (or in `~/Music/PHI Edits` before the mix is saved); `Ctrl+Z` in the mixer undoes it.

## Instruments

[]{.ix t="Instruments!on mixer tracks"}Each track has an instrument that its MIDI clips play, and that the MIDI keyboard plays when the track is armed (`R`, the dot):

- the built-in **Synth**: sine, triangle, saw or square through a low-pass, with General MIDI drums on channel 10, so any MIDI file sounds at once;
- a **Sampler** or **Slicer** of the track's own, with its sound from a file or from the Sample menu; Edit opens the usual window for it;
- any **VST3, LV2 or CLAP instrument** found by the plugin scan, including PHI's own instruments (see [Instruments](#instruments)); Edit opens its window;
- an **Instrument Prism**: several instruments at once, in rays (see below).

### Instrument Prisms {#instrument-prism}

[]{.ix t="Instrument Prism"}[]{.ix t="Prisms!instrument"}An Instrument Prism is a track's instrument made of **rays**, like Ableton's Instrument Rack: layers or keyboard splits, each with effects of its own. Choose **Instrument Prism** from the instrument menu (▾) on the track's strip or its Device Chain card, or drag it from the browser's Instruments (Built-in). The instrument the track had moves into the first ray and keeps its settings; a track with a Sampler, a Slicer or nothing starts with a Synth.

- **A ray** is a chain in series: an instrument (the Synth, PHI rāga or an instrument plugin), then up to eight effects heard only on that ray, each with bypass and Mix.
- **A second instrument.** **+ Instrument** adds a B instrument beside the first. **Mix** sets the balance between A and B: in the middle both play in full, and each fades out towards the other end (double-click for the middle). **Split at** a key gives A the notes below it and B the notes from it up.
- **Key and velocity ranges.** A ray plays only the notes in its key range (C−1 to G9, middle C is C4) and its velocity range, so rays can split the keyboard or layer by how hard you play. Type a note name such as `C3`, or a number.
- **Gain, mute and solo** for each ray, and a level bar. The rays are added together; a ray whose effects have latency delays the others to match, so layers stay together.
- Effects in the track's inserts, after the Prism's card, process all the rays together, as an effect after a rack does in Ableton.

Docked, the Prism's card in the Device Chain lists its rays. **Open** shows them in a window, a row a ray: its name, instrument (▾ to choose another, **Edit** to open it), effects (click one to open, bypass, set its Mix, move, replace or remove it), **+ Effect**, the ranges, gain, **M**, **S**, and ↑ ↓ ✕ to move or remove the ray. **+ Add Ray** (`Ctrl+T`) adds a ray with a Synth, up to eight. The rays, instruments and effects are saved with the project. A Sampler or Slicer cannot go in a ray yet; keep it on the track itself.

## The mixer's Device Chain {#mixer-device-chain}

[]{.ix t="Device Chain!in the mixer"}Along the bottom of the mixer runs its own Device Chain, the whole width, for the selected track: its instrument first (**Edit** opens it, ▾ picks another), then its inserts in the order they play, each card with ⏻ (on or bypassed), **Edit**, up to eight of the effect's knobs and its Dry/Wet. Right-click a card to move, replace or remove it; **Add effect** ends the row. `Ctrl+Shift+C` hides or shows it, and PHI remembers which. While the mixer is in front the editor's own Device Chain (the master's) steps aside, so you never see two.

[]{.ix t="Clips!picture"}Clicking a clip on the tracks or in the launcher draws it there instead: an audio clip's waveform, or a MIDI clip's notes, with what can be done to it beside it (for a MIDI clip: Φ, Duplicate, ½ and ×2, a mode or rāga, Reverse, Counterpoint, Fugue and **Edit Notes...**). Clicking a strip, a track's name or an empty slot brings the chain back.

`Shift+Tab` (outside a box you type in) switches the space between the Device Chain and the Simplest or Slicer docked there; see [Both together](#samplers).

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
| `R` | Arms the track (with a clip picked, reverses the clip) |
| `E` | Edits the instrument |
| `A` / `I` | Adds an audio or a MIDI file at the playhead |
| `B` / `Shift+B` | Bounces in place / to a new track (see [Bouncing](#mixdown)) |
| `Insert` | Adds an insert effect |
| `Delete` | Deletes the selected track |
| `=` / `-` | Makes the strips wider or narrower |
| `C` | Turns the click on or off |

The master has a fader and meter and no plugins: mastering stays the editor's job.

### Adding and deleting tracks {#mixer-tracks}

[]{.ix t="Tracks!adding and deleting"}A new mix starts with two tracks. Mixer > Add Track, the **+ Track** button or `Ctrl+T` adds one after the selected track. Mixer > Delete Selected Track, the **− Track** button, `Ctrl+Shift+T`, `Delete` with the strips focused, or **Delete Track** on a strip's right-click menu deletes the selected one, with its clips, instrument, inserts and launcher slots; the tracks after it move up. A mix keeps at least one track.

Deleting a track can be undone: `Ctrl+Z` in the mixer brings it back where it was, with everything it had.

### Sixteen tracks {#mixer-sixteen}

[]{.ix t="Sixteen tracks"}[]{.ix t="Tracks!sixteen"}A mix has room for eight tracks at first, because the Selector's sets, the Φ button's scenes and the Compose methods are built around eight parts. For more, open the **Strips** button's menu and tick **16 Tracks**: `Ctrl+T` then adds tracks up to sixteen. The setting is saved with the project. It can go back to eight only once tracks 9 to 16 are deleted.

When something needs more tracks than eight leave free (Add Track, Bounce to New Track, or Counterpoint, Fugue, the 12-tone row, Stochastic and Minimalize writing their voices), PHI offers to go to sixteen. The patchbay has a node for each of the sixteen tracks; a project saved before sends tracks 9 to 16 to the Mix Bus, as by default.

The number keys pick tracks 1 to 8; click a strip, or use `Left` and `Right`, to reach the others.

### Zooming the strips {#strip-zoom}

[]{.ix t="Strips!zoom"}As in Cubase's MixConsole, `=` and `-` make the strips wider or narrower, and so does `Ctrl` + wheel over the strips or the launcher. The launcher's columns are always as wide as the strips and scroll sideways with them. The strip under the mouse (or the selected one, from the keys) stays where it is while the others move. **Strips to Fit the Window**, in the Strips menu, goes back to fitting them all in, as at first.

As strips narrow they keep what matters: below a certain width a strip shows only its pan, mute, solo, **PM**, fader and name; narrower still, only the fader, a thin meter, mute, solo and its number.

`Ctrl+=` and `Ctrl+-` zoom the tracks across time instead, apart from the strips.

### Focus follows the mouse, and arming {#mixer-arm}

[]{.ix t="Focus Follows Mouse!mixer strips"}[]{.ix t="Arm the Selected Track"}Two more choices in the Strips menu:

- **Focus Follows Mouse** (off at first): pointing at a strip selects its track, so the keys and the Device Chain act on it without a click.
- **Arm the Selected Track** (on at first), like Ableton's auto-arm: selecting a track that plays an instrument arms it, so the MIDI keyboard plays it and Record records it. An audio-only track leaves the arm where it was. Pointing at a strip never arms it, and nothing changes the arm while a take is recording.

### The Pre-Master {#pre-master}

[]{.ix t="Pre-Master"}The **Pre-Master** is a bus strip of its own: tracks sent to it are mixed there first, through its own inserts, mute and fader, before the master. Click **PM** on a track's strip, or choose **To the Pre-Master** on its right-click menu, to send it there instead of straight to the master. The Pre-Master's strip lists the tracks it takes.

All of it, the cuts, delay, width, inserts with their settings, sends, modes and ducking, is saved with the project.

**Transport.** `Space` plays and stops, `Home` goes to the start, `L` loops the whole mix. The tempo box sets the bars and beats; **MIDI follows tempo** plays the MIDI files at it instead of their own tempo maps.

## Spatial mixing: surround, binaural and Ambisonics {#spatial-mixing}

[]{.ix t="Spatial mixing"}[]{.ix t="Surround!in the mixer"}[]{.ix t="Binaural!in the mixer"}The mixer can place every track around you in 3D, heard on headphones and on a speaker array at the same time.

::: steps
1. Press **Spatial...** in the mixer's top bar and tick **Spatial mixing in this project**. The button stays lit while the project is spatial, and the project remembers it.
2. Under **Headphones**, pick what the stereo out (outputs 1 and 2, and Mix Down) carries: **Binaural (HRTF)** through an HRTF set (a `.sofa` file; **Other...** picks any), **Stereo decode** for two speakers, or **Off**.
3. Under **Speakers**, pick your array. Make one first in Spatial > Speaker Arrays (or **Speaker Arrays...**): each speaker's name, azimuth and elevation, up to 16, or a standard layout from Quad to 9.1.6. **First speaker on output** is the device output of the first speaker; leave it at 3 so the headphones keep outputs 1 and 2. The audio device has to offer that many outputs (PipeWire or JACK, chosen in Options > Audio Settings).
4. **Turn the scene** turns everything at once.
:::

The first time a project goes spatial, each track goes where its pan had it, and centred tracks spread across the front.

**Placing the sounds.** Each track, the Pre-Master and the two returns get a 3D panner in place of pan and width:

| Control | Sets |
|---|---|
| **Azimuth** | Where it is around you: 0° ahead, 90° to the right, ±180° behind |
| **Elevation** | 90° overhead, −90° below |
| **Spread** | How far apart a stereo source's two sides are (0° makes it a point) |
| **Distance** | 1 is the speakers' ring; nearer spreads it around you, farther falls 1.5 dB a unit, out to 10 |
| **LFE** | A send after the fader to the subwoofers in the array, below 120 Hz |

The strips show the azimuth; the Inspector shows every control for the selected track, with the room seen from above. In the room (also in the Spatial dialog), drag a source to place it (`Shift` keeps its distance), turn the wheel over it to raise or lower it, `Shift`+wheel to widen it, and double-click to put it back ahead.

**Surround and ambisonic files on a track.** A file of 5.0 to 9.1.6, Quad, or first- to third-order Ambisonics (AmbiX) keeps all its channels on its track; its strip and clip say which (`5.1`, `7.1.4`, `AmbiX 3rd`). In spatial mode its front left and right go through the strip like any stereo track, and its other speakers go straight into the scene where they belong, turned by the track's azimuth; trim, fader, mute and solo act on every channel, and its LFE goes to the subwoofers. An ambisonic file goes into the scene whole. Outside spatial mode, and in bounces, a surround file is heard folded down to stereo.

**Files of many channels.** A poly WAV from a field recorder or a Dante or MADI capture is heard folded down; dropping it on a track asks whether to put each channel on a track of its own. **Split Channels to Tracks**, in the clip's tools, does the same for any file of more than two channels: each channel becomes a 24-bit mono WAV beside the mix, named by its speaker (`L`, `C`, `LFE`…) when the layout is known.

**Metering and Mix Down.** The Spatial dialog has a meter for each speaker of the array. Its Mix Down choice renders the headphone mix (stereo), the speakers (a channel a speaker, so a 5.1.4 or 7.1.4 file), or the scene itself as third-order AmbiX (16 channels). The new tab is labelled with that layout, ready for the [Spatial](#spatial) tools.

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

[]{.ix t="Patchbay"}Mixer > Patchbay (`F4`) is a grid that routes everything PHI plays live. It is the router itself, not a view of it: the mixer's tracks, the samplers, the synths and the editor's playback all go through it.

![The patchbay: sources down the side, destinations across the top.](images/patchbay.png)

| Point | As a source (row) | As a destination (column) |
|---|---|---|
| Editor: the open file, after the Master Section | yes | |
| Sampler and Slicer | yes | |
| Synth 1 to 4: the patchbay's own instruments | yes | |
| Track 1 to 16: out after the fader, in before trim, pan and fader | yes | yes |
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
