# The Clip Launcher

[]{.ix t="Clip Launcher"}[]{.ix t="Scenes"}The clip launcher plays loops and phrases in a grid, in the style of Ableton Live's Session View: tracks across, scenes down, each slot a clip that starts in time with the others when you launch it. It is made for live performance, and for building a song from parts before it is arranged.

Performance > Clip Launcher (`F5`) opens the mixer with the launcher in its upper area. `F5` again goes back to the editor. In the mixer, the **Launcher** button switches between the launcher and the tracks across time, and `V` goes back to the tracks.

![The clip launcher: the rack on the left, the grid in the middle, compact strips below.](images/clip-launcher.png)

## The layout

- **Left: the rack** of the selected track: its instrument (with Edit and the MIDI keyboard arm), the track (trim, transpose, MIDI channels, polarity, Back to Timeline) and the clip in the slot under the cursor (loop, reverse, its own launch quantise, choose a file, empty the slot).
- **Middle: the grid.** Tracks across with their names and colours, scenes down. Each scene has a launch button on the right; each track has a stop button under its column, and **Stop All** is under the scenes. A ⟲ in a track's header means it is off its timeline; click it to go back.
- **Below: compact strips**: name, pan, mute and solo, fader and meter.
- The top bar adds the **launch quantise** and the number of **scenes** (1 to 16, 8 to start).

## Filling slots from the Selector

[]{.ix t="Selector!and the clip launcher"}This is the launcher's heart. With the launcher showing, `Enter` or a double-click on a file in the Find panel puts it into the first empty slot of the track for its Selector colour, instead of opening it in the editor:

1. The colour is the one shown in the Selector, or else the file's first colour mark.
2. The track is the one named like that colour in the chosen set (DR, BA, PR… in Live Performance), or else the one with that colour, or else the selected track.
3. The slot is the first empty one from the top. A full column gets one more scene, up to 16.
4. The mixer follows: that track is selected, and the cursor is on the new slot.

So one pass down the Selector, one pick per colour, builds a scene across the tracks; picking again in a colour fills the next scene down. Start with Mixer > New Project from Selector Set, and every colour has a track ready for it.

An audio file becomes an audio clip. A MIDI file becomes a MIDI clip that plays the track's instrument (the Synth, for a track that has none), made whole bars long at the tempo it plays at.

## Playing

| Action | Mouse | Keys |
|---|---|---|
| Launch a clip | Its ▶, or double-click it | `Enter` on the cursor |
| Launch a scene | The scene's ▶ | `Shift+Enter` |
| Stop a track | Its stop button | `.` |
| Stop every track | Stop All | `Shift+.` |
| Back to the timeline | The ⟲ or the rack's button | `T` (all tracks: `Shift+T`) |
| Empty a slot | Right-click | `Delete` / `Backspace` |

[]{.ix t="Launch quantise"}**Launch quantise.** A launch waits for the next quantise point: off, 1/16 note up to 8 bars, 1 bar to start, in 4/4 at the mix's tempo. A clip can have its own. The switch happens on the exact sample.

- One clip plays per track. Launching a scene launches its whole row; a track with an empty slot in that scene stops.
- A **looping** clip goes round until stopped; one that plays **once** stops its track at its end.
- Launching with the transport stopped starts it, and the clip plays at once. Stopping the transport stops every clip.
- A waiting clip blinks; a playing clip has a green ▶, a progress bar along its bottom and its pass count (×2, ×3…).

**The timeline.** Launching a clip takes its track off the timeline clips, as in Ableton: they are not heard, even once the clip has stopped, until **Back to Timeline**. Mix Down renders the timeline.

## Other keys

| Keys | Does |
|---|---|
| Arrows | Move the cursor; the strip and the rack follow its track |
| `Ctrl+Up` / `Ctrl+Down` | Move the selected fader (`Shift` for finer steps) |
| `1`–`8`, `M`, `S`, `R`, `E`, `Space`, `Home` | As everywhere in the mixer |

Right-click a clip for loop, reverse, its launch quantise, a file and emptying it. Right-click or double-click a scene's name to rename it. Files dropped on a slot fill it and the empty slots below.

The launcher, its scenes and slots are saved in the project.
