# PHI Clip Launcher

Status: Tier 1 built (2026-10-22). The design, with a clickable mock and the survey of other
launchers, is the owner's "PHI Clip Launcher" page; this file is what the code does.

## What the owner asked for (2026-10-22)

"Start designing the clip launcher and pick out the best features for live performance from all
the clip launchers surveyed. Optimized for live performance. It will be in the style of Ableton,
but the racks on the left. The main feature will be to integrate with the Selector, based on the
Selector set: the user starts with the top, hits enter or double clicks the sample desired and it
automatically enters into the first row of the corresponding clip, the mixer follows this. The next
chosen category goes to the next corresponding clip slot in the launcher, etc."

Earlier: the launcher view shows just the clips, with a compact mixer below them (fader, meter,
pan, mute and solo).

## Where it is

Performance > Clip Launcher (F5) opens the mixer with the launcher in its upper area, where the
tracks across time usually are. F5 again goes back to the editor. In the mixer, the Launcher button
switches between the two, and V goes back to the tracks across time.

Tab switches between the launcher and the tracks across time, as in Ableton (2026-10-25).

- **The grid.** The scenes on the left (owner, 2026-10-25), each with its launch button, and Stop
  All under them; then the tracks across with their names and colours, scenes down, each with a
  stop button under its column. A clip is filled with its track's colour. A ⟲ in a track's header
  means it is off its timeline; clicking it goes back. The grid scrolls when the strips need the
  room.
- **The Inspector** (owner, 2026-10-25; the top bar's Inspector button, on to start): one column
  at the left of the mixer, in the launcher and in the tracks view alike. At the top the selected
  track's name on its colour, then its mode (Audio, Inst, Hybrid) with the layer and Duck dials,
  its instrument (with Edit, transpose, MIDI channel and the keyboard arm), Pre (polarity, low and
  high cut, track delay), all its inserts, Send A and B, and width. In the launcher it also shows
  the track's timeline follow (Back to Timeline) and the clip in the slot under the cursor (loop,
  reverse, its own launch quantise, choose a file, empty the slot). The Strips button picks its
  sections too.
- **The Device Chain** (owner, 2026-10-25) along the bottom, the whole width, as in Ableton: the
  selected track's instrument, always first on the left, then its inserts left to right (up to
  eight knobs each and Dry/Wet), then Add effect. Clicking a clip puts a drawing of it there
  instead: an audio clip's waveform or a MIDI clip's notes in the track's colour, not editable.
  An empty slot, a track's name or its strip brings the chain back. With the Master strip
  selected it steps aside for the editor's own chain. More in mixer.md.
- **Below: the strips, made compact** (owner, 2026-10-25): four insert slots as tall as the clip
  slots, Send A and Send B above Trim and Pan, mute and solo, a shorter fader and meter with their
  values, and the track's name at the bottom in white on its colour. The full strips come back
  with the tracks across time.
- In a see-through window only the track names and the clips stay solid.
- The top bar gains the launch quantise, the number of scenes (1 to 32, 16 to start) and the
  Inspector.

## The Selector

With the launcher showing, Enter or a double-click on a file in the Find panel puts it into the
first empty slot of the track for its Selector colour, instead of opening it in the editor:

1. The colour is the one shown in the Selector (the sidebar entry chosen), else the file's first
   colour mark.
2. The track is the one named like that colour in the set chosen (DR, BA, PR... in Live
   Performance), else the one with that colour, else the selected track. A project made with
   Mixer > New Project from Selector Set has a track for every colour, named and coloured to match.
3. The first empty slot from the top. A full column gets one more scene, up to 32.
4. The mixer follows: that track is selected (its strip, the rack) and the cursor is on the slot.

So one pass down the Selector, one pick per colour, builds a scene across the tracks; picking again
in a colour fills the next scene down. The mixer's own file browser does the same for the selected
track while the launcher shows.

### The Φ random selector

The Φ button at the end of the Selector line deals a whole scene at once: one of each colour's
favourite files in the set shown, picked by the golden ratio, into the first scene that is empty
on all their tracks. Press it again for the next scene, with another combination. With the
launcher hidden it deals onto the mixer's tracks instead, all starting together after everything
already on them, so each deal follows the last. Shift+click deals again in place of the last
deal; right-click chooses the launcher, the tracks or a Prism. A colour with no track of its own
gets the track a project from the set would give it (added, named and coloured when the mix has
fewer).

Instruments deal too: an instrument plugin, or a sampler or synth from the Sample and Synth menus,
marked with a colour is loaded on that colour's track before the files arrive, so a MIDI file dealt
with it plays on it. **Φ Deal a Prism of Effects** (right-click, or a click when only effects are
marked) builds a Prism in the first empty Master Section slot: one of each colour's favourite
effects on a ray of its own named for the colour, in parallel, each ray turned down so that
together they are about as loud as one.

Each colour walks its favourites by a sequence of its own, frac (start + n α): the first colour by
α = 1/φ, the others by √7, √13, √19, √29, √41, √89 and √73, quadratic irrationals like φ. Each
colour spreads evenly over its favourites and never deals the same one twice running (with three
or more), and the colours never fall into step, so with eight favourites per colour there are
8⁸, nearly 17 million, scenes to deal. Every session starts somewhere new.

A WAV (or any audio file PHI reads) becomes an audio clip; a MIDI file becomes a MIDI clip that
plays the track's instrument (the Synth is chosen for a track that has none). A MIDI loop is made
whole bars long, at the tempo it plays at.

## Playing

- **Launching.** A click on a clip's ▶, a double-click on the clip, or Enter on the cursor launches
  it; one clip plays per track. A scene's ▶ (or Shift+Enter) launches its whole row; a track with an
  empty slot in that scene stops.
- **Launch Quantise.** A launch waits for the next quantise of the launch clock: off, 1/16 note up
  to 8 bars, 1 bar to start, in 4/4 at the mix's tempo. A clip can have its own. The switch happens
  on the exact frame (the audio block is split there), with the clips' usual short edge fades.
- **The launch clock** runs while the transport plays and starts again at 0 with it. Launching with
  the transport stopped starts it, and the clip plays at once.
- **Loop or once.** A looping clip goes round until stopped; one that plays once stops its track at
  its end.
- **Stopping.** A track's stop button (or .) stops it at the next quantise; Stop All (Shift+.) every
  track. Stopping the transport stops every clip.
- **The timeline.** Launching a clip takes the track off its timeline clips, as in Ableton: they are
  not heard, even once the clip is stopped, until Back to Timeline (T for the selected track,
  Shift+T for all, the rack's button or the ⟲). The transport keeps going past the end of the
  timeline while anything is launched. Mix Down renders the timeline.
- **Feedback.** A waiting clip blinks, a playing clip has a green ▶, a progress bar along its
  bottom and its pass count (×2, ×3...), and a stop that is waiting blinks its stop button.

## Keys in the launcher

| Key | Does |
| --- | --- |
| Arrows | Move the cursor; the strip and the rack follow its track |
| Enter | Launch the slot under the cursor (an empty one stops the track) |
| Shift+Enter | Launch the cursor's scene |
| . | Stop the cursor's track; Shift+. stops every track |
| Delete, Backspace | Empty the slot |
| Ctrl+Up, Ctrl+Down | Move the selected fader (Shift for finer steps) |
| T, Shift+T | Back to Timeline for the track, for every track |
| 1-8, M, S, R, E, Space, Home | As everywhere in the mixer |

Right-click a clip for loop, reverse, its launch quantise, a file and emptying it; right-click or
double-click a scene's name to rename it. Files dropped on a slot fill it and the empty slots below.

## In the project

A project (.phi, `phinyxMix` 3) has a `launcher` section: the number of scenes, the quantise in
beats, the scene names, whether the launcher was showing, and the slots, each with its track and
scene, its file (relative to the project, like the clips), offset and length in seconds, reversal,
loop and own quantise (-1 for the launcher's). Older projects open with an empty launcher.

## Engine

`mixer::Engine` keeps a slot (a `mixer::Slot`: a `mixer::Clip` plus loop and quantise) for each
track and scene. The UI sets slots (an edit, under the engine's lock) and launches (a queue the audio
thread takes when it can, so a launch never holds up the sound). Each track keeps the clip playing,
the one waiting and its launch frame; the clip's passes are placed on the launch clock block by
block, so loops, MIDI notes held over a pass and the notes a stop lets go of all go through the same
code as the timeline. `launchInfo()` gives the UI what plays, what waits, the pass and the progress.
The tests are in tests/CoreTests.cpp (`testLauncher`).

## Next (Tier 2 and 3 of the design)

Launch Styles (Trigger, Gate, Toggle, Repeat), Clip Actions with chances, scene tempo and time
signature, MIDI learn and a generic pad layout with lights, audio clips stretched to the tempo,
Record Launcher to Timeline; then independent columns, group scenes, Capture Scene, scene chains,
Pick for me and effects in the rack. Recording into slots is built: [recording.md](recording.md).
