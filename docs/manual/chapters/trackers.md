# The Trackers

[]{.ix t="Tracker"}Trackers write music as patterns: columns of notes, one row per step, read from top to bottom. PHI has two. **Tracker1** is built on MilkyTracker and lives inside PHI. **Tracker2** is Furnace, the chiptune tracker, in a window of its own.

## Tracker1 {#tracker1}

[]{.ix t="Tracker1"}[]{.ix t="MilkyTracker"}Tracker menu > Tracker1 (`Ctrl+Alt+Shift+K`) opens XM, MOD, IT, S3M and the other module formats MilkyTracker reads, plays them through PHI, and shows the patterns in the classic FastTracker 2 layout (note, instrument, volume, effect) in PHI's theme, following what plays.

![Tracker1 with a new song: the order list, a pattern in the FastTracker 2 layout, and the instruments.](images/tracker1.png)

### Playing and recording

| Keys | Does |
|---|---|
| `Space` | Plays the song |
| `Enter` | Plays the pattern |
| `F` | Turns following on or off |
| `M` / `S` | Mutes or solos the channel |
| `T` | Chooses the mixer track the channel records to (1 to 8) |
| `Ctrl+R` | Record to Editor, stereo |
| `Ctrl+Shift+R` | Record to Editor, multi-channel |

**Record to Editor** records the whole song into a new tab: in stereo, or a stereo pair per track (up to 16 channels).

### Editing

Editing works like FastTracker 2.

| Keys | Does |
|---|---|
| `Ctrl+N` | New song |
| `Shift+Space` | Editing on or off |
| `Z` to `M`, `Q` to `P` | The keyboard as a piano, by key position |
| `F1` to `F8` | Choose the octave |
| `1` or `` ` `` | Key off |
| Hex digits | Fill the instrument, volume and effect columns |
| `Delete` | Clears |
| `Insert` / `Backspace` | Push and pull rows |
| `Tab` | Next channel |
| `Shift` + arrows | Select |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy, cut, paste |
| `Alt+Up` / `Alt+Down` | Transpose |
| `Ctrl+Z` / `Ctrl+Y` | Undo, redo |
| `Ctrl+L` | Loads an audio file into the current instrument |
| `Ctrl+S` | Saves the song as XM |

Notes sound as you type them. The order list adds, removes and changes patterns.

### Tracker1 and the Retro Sampler {#tracker-ami}

[]{.ix t="Retro Sampler!with Tracker1"}An instrument can play through one of the Retro Sampler's 12 slots instead of its own sample. Right-click it, or use the instrument list's **Retro Sampler** button, and choose a slot. Its notes then go to that slot as MIDI (slot 3 on MIDI channel 3, C-4 as note 60, the volume as velocity) on the tick they fall on, including note delay and retrigger; key off and note cut let go, and mute and solo apply. Linked instruments show `[AMI n]`.

The Retro Sampler plays in a patchbay synth slot, and is loaded there if it is not yet, with each slot listening on its own channel (so a MIDI keyboard on channel 1 plays slot 1). Notes typed in sound through it, and Record to Editor records it. Saving the song writes the links and the Retro Sampler's state beside it as `song.xm.ami`, read back when the song opens.

::: note
Volume and pitch effects do not reach the Retro Sampler yet. Instrument envelopes and a sample editor are planned.
:::

## Tracker2 {#tracker2}

[]{.ix t="Tracker2"}[]{.ix t="Furnace"}Tracker menu > Tracker2 opens **Furnace**, the chiptune tracker for over 80 sound chips (Genesis, NES, Game Boy, C64, Amiga, arcade FM and many more), in its own window with its own look. PHI builds it from its source.

A thin opacity bar runs along the bottom of its window, as on the instruments' windows. Furnace keeps it in its own settings.

Furnace is its own program and plays through the desktop's sound (PipeWire, JACK or ALSA), so it stays open when PHI closes, and Record to Editor does not record it. To capture it, record it as an application with File > Record or with Performance (see [Recording](#recording)), or export from Furnace.
