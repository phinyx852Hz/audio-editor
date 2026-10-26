# PHI Mixer: design draft

Status: step 1 built (2026-10-21); the rest is the proposal the owner reviewed (2026-10-19).
See "Built so far" at the end.

## One kind of track (owner, 2026-10-21)

"Create 8 track mixer to host a unified track architecture: a track can host either an audio
file or a MIDI file, but there will be no MIDI editor. It can play back MIDI on a file or audio
on a file, or both on the same file, like Reaper or Bitwig or Tracktion."

So there are no audio tracks and MIDI tracks, only tracks. A track is a timeline of clips plus
an instrument (owner, later the same day: "I thought it would be serial", as in Reaper and
Tracktion):
- **Clips**: any number, each an audio file or a MIDI file (.mid, types 0 and 1), placed one
  after another or overlapping, each trimmed to the part heard and mutable on its own. Audio clips
  play as they are; MIDI clips play, never edited, into the track's instrument, with the track's
  transpose (channel 10, the drums, stays) and choice of its 16 channels.
- **Instrument**: what turns MIDI into sound, from the MIDI clips and from the MIDI keyboard when the
  track is armed: the built-in Synth (with General MIDI drums), the track's own Sampler or Slicer,
  or a VST3, LV2 or CLAP instrument plugin.
Files go on with the mouse (dragged from the browser or the file manager onto a row) or the
keyboard (Enter in the browser puts the file on the selected track at the playhead).
This replaces the single "Source" (stem, live input or instrument) below: a stem is an audio
clip, an instrument is the instrument, and a live input will record into new clips.

## What it is

A small mixer in its own window: **up to 8 tracks and a master out (track 9)**. A new mix
starts with 2 tracks; Ctrl+T adds one (up to 8) and Ctrl+Shift+T removes the selected one. It mixes bus stems
from another DAW, or works as an instrument for composing, producing and performing. The
mix goes straight into the PHI editor, where the Master Section does the mastering.

On purpose, it has none of these:
- No MIDI editing. An instrument plugin can be loaded and played from a MIDI keyboard, but
  a track only carries audio.
- No plugins on the master: no limiter either. The editor's Master Section does that job.
- No timeline editing. Stems are placed whole; cutting and fixing happen in the editor.

Sources read for this draft: the owner's guides to the Ableton Live, Adobe Audition, Ardour,
Cubase and Harrison Mixbus mixers. What the owner likes most:
- Ableton's minimalism.
- Audition's plain fader and meter.
- Cubase's Pre section.
- Harrison's spatial panner and spectral thumbnail.

## The window

- **Opening it:** View > Mixer. It is a separate window that floats by default and can go full
  screen. Like the main window, it can dock any panel (meters, Find, the visualizer).
- **Shared settings:** it uses the same theme, opacity and visualizer-behind setting as the main
  window.
- **Transport:** play, stop, loop and locate, shared with the editor's keys (Space, Home, End).
- **Hiding sections:** each section of the strips can be shown or hidden across all strips at
  once, as in Ableton and Cubase, so the mixer can be as small as name, fader and meter.
- **Strip width:** narrow or wide. Wide shows more of the thumbnail and the numbers.

## A track strip, top to bottom

1. **Header**
   - Number, name and colour.
   - Source: Stem file, Live input (mono or stereo, from the interface), or Instrument
     (a VST3/LV2/CLAP instrument played from a MIDI keyboard; it outputs audio only).
   - A stem starts at 0. An offset in samples or milliseconds lines it up when another DAW
     exported it late.
2. **Pre (Cubase)**
   - **Trim dial**, ±24 dB, before everything else. This is the gain-staging control; the
     fader is the mix level.
   - **Polarity Ø**, per channel on stereo tracks.
   - **Low cut and high cut**, off by default, with frequency and slope (12 or 24 dB/oct).
   - **Track delay**, ± milliseconds (Ableton), for lining up or for groove.
   - **Monitor** (live input only): Off, Auto (when armed) or In.
3. **Inserts**: a slim list of plugin slots, folded to one line until opened. This is the
   same plugin host and floating plugin windows as the Master Section (Esc closes a window).
   Plugins run before the fader.
4. **Thumbnail (Harrison)**: a small live display.
   - Click it to step through: spectrum, vectorscope/goniometer, VU, PPM (EBU, DIN or
     BBC ballistics) and correlation.
   - Right-click picks a display from a list. Double-click opens it large in a floating
     window.
   - It reuses PHI's existing goniometer, spectrum and meter code.
5. **Sends (Ableton)**: two send dials, **A Reverb** and **B Delay**, from −∞ to 0 dB, and
   post-fader by default. Right-click a dial to switch it to pre-fader.
6. **Panner**: both kinds are on every strip, with no switching between them.
   - **Pan pot**, a traditional knob. Mono tracks use an equal-power law (−3 dB in the
     middle; −4.5 and −6 dB laws are options). Stereo tracks use balance.
   - **Width dial** (stereo tracks only), from 0 % (mono) to 200 %. It is hidden unless
     the Panner section is wide.
   - **Spatial panner (Harrison)**: a small live picture under the pot that you can drag;
     the pot and the picture move together. Double-click it to open it large. With a stereo
     master it shows position and width as a stereo image. With a surround or ambisonic master it
     becomes a top-down room (5.1 to 9.1.6) or a sphere (ambisonics), using PHI's
     existing Spatial code.
7. **Mute, Solo, Record**. Solo works like a console: soloing a track mutes the others,
   and any number of tracks can be soloed.
8. **Fader and meter (Audition)**
   - A thin, long-throw fader from −∞ to +6 dB, with a peak and RMS meter beside it.
   - The meter has peak hold and a clip light; clicking the clip light clears it.
   - Underneath: the fader value and the highest peak, as text. Typing on a selected strip
     sets the value exactly.

## The return strips: A Reverb and B Delay

These are two narrow strips between the tracks and the master, as Ableton's return tracks
are. Each one holds a single effect, fully wet, then a pan pot, mute, a fader and a meter.
- **A** starts with PHI's built-in reverb (a plate, about 1.8 s long).
- **B** starts with a tempo-synced delay (a dotted eighth, with feedback at 30 %).
- Either effect can be swapped for any plugin. The templates set their own (the podcast
  templates use a short room reverb and no delay).
- More sends and returns can come later; two cover most mixes.

The master still has no plugins. The returns are part of the mix that feeds it.

## The master strip (track 9)

- **Output format:** Stereo, 5.1, 7.1, 7.1.4, 9.1.6, Ambisonics (orders 1 to 3) or
  Binaural. The format decides which spatial panner the tracks get.
- **Display:** the same thumbnail choices, plus loudness (momentary and short-term LUFS) and
  true peak.
- **Level:** a fader and meter. There are no plugins.
- **Mix Down to Editor:** renders the mix and opens it as a new file in PHI.
  - When every track is a stem, the mix renders faster than real time.
  - When a track is a live input or an instrument, the mix is recorded in real time while
    you play. That is how a performance is captured.

## Templates

A template stores everything about the 8 strips: names, a one-line role under each name,
colours, sources, Pre settings, inserts with their settings, thumbnail choices, pan, and
the master's output format. File > New Mix from Template lists them. Any mix can be saved
as a new template, and templates are plain files you can share.

Built in:

**Creative Organism** (electronic music; owner's template, after ill.Gates' idea of
reusable systems and his Mudpies technique). The names describe intentions, not instrument
types. The role line shows under the name on the strip:

| # | Name | Role line | Colour |
|---|---|---|---|
| 1 | SNOWFLAKES | Micro-details · ear candy · happy accidents | ice white |
| 2 | EARTHQUAKES | Drums · impacts · rhythmic power | red |
| 3 | GRAVITY | Bass · sub · low-frequency weight | deep purple |
| 4 | ATMOSPHERE | Pads · drones · harmonic environment | teal |
| 5 | FIREFLIES | Leads · arpeggios · melodic fragments | amber |
| 6 | MUDPIES | Resampling · mutation · sound experiments | brown |
| 7 | WEATHER SYSTEM | Builds · sweeps · transitions · tension | blue-grey |
| 8 | WILDCARD | Improvisation · live input · unexpected events | magenta |

Starting settings for that template: Gravity is mono-summed below 120 Hz, with low cut
off. Snowflakes and Atmosphere have low cut at 150 Hz. Wildcard's source is a live input,
with Monitor set to Auto.

**DAW Stems** (mixing bus stems): Drums, Bass, Music, Keys, Guitars, Vocals, FX,
Ambience. Every source is a stem file, and each strip is set flat.

**Default** (a new mix): Track 1 and Track 2, both flat, with stem files as their sources.

**Rock**: Kick & Snare, Drum Room, Bass, Rhythm Guitars, Lead Guitar, Keys, Vocals,
Backing Vocals. Drum Room has low cut at 120 Hz. Bass is mono. Both guitar strips have low
cut at 80 Hz. Vocals have low cut at 90 Hz.

**Folk**: Acoustic Guitar, Banjo / Mandolin, Fiddle, Upright Bass, Percussion, Room Mics,
Lead Vocal, Harmonies. Everything is kept natural: low cut at 60 Hz, and none on the
bass. Room Mics is panned wide, and Upright Bass is mono.

**Orchestral + Synths** (group stems): Strings, Brass, Woodwinds, Percussion, Choir /
Keys, Synth Pads, Synth Bass / Leads, Hall / Ambience. The pan pots start at the usual
stage seats: violins left, cellos and basses right, brass right-centre, woodwinds centre.
The Hall / Ambience strip is stereo at full width.

**Podcast** templates. All are voice-first: low cut at 80 Hz, polarity checked, a voice
chain in each strip's inserts (de-esser, then a gentle compressor), pan centred, and the
master set to Stereo.
- **Solo Host**: Host, Music Bed, SFX / Stingers, Ads.
- **Interview**: Host, Guest, Music Bed, SFX.
- **Roundtable**: Host, Guest 1, Guest 2, Guest 3, Music Bed, SFX. The voices are panned
  slightly apart (±15) so listeners can tell them apart.
- **Remote Guest**: Host (live input), Remote Guest (a call recording or live input),
  Music Bed, SFX. The remote guest strip adds a noise gate.
- **Narrative / Audio Drama**: Narrator, Voice 1, Voice 2, Voice 3, Ambience, Foley,
  Music, SFX.

More templates can be added in the same way; each is a small file.

## Keyboard (PHI is keyboard-first)

| Keys | What they do |
|---|---|
| 1–8 | Select a track |
| 9 | Select the master |
| Left / Right | Previous / next strip |
| Up / Down | Move through the sections of the selected strip (trim, inserts, thumbnail, pan, fader) |
| + / − | Change the focused control (Shift for finer steps) |
| 0 | Reset the focused control |
| M / S / R | Mute, solo, record |
| T | Step the thumbnail display |
| V | Show or hide the tracks across time above the strips |
| Ctrl+B, Ctrl+F | The Finder or the Inspector in the left column |
| F6 | Next section: browser, tracks, strips (Shift: back) |
| Tab | Switch between the clip launcher and the tracks across time (Ableton) |
| Ctrl+Z, Ctrl+Shift+Z | Undo, redo clip edits |
| B | Bounce in place: the track's sound (MIDI through its instrument, and its audio) becomes its audio |
| Shift+B | Bounce to a new track and mute this one (not when all 8 tracks are in use) |

Every control has a screen-reader name and announces its value.

## Left for later, unless the owner wants them now

- Automation (recording fader moves).
- Mix snapshots, A/B (cheap to add: store and recall all strip settings).
- Groups and VCAs. With 8 tracks these matter less.

## Build order

1. Engine: 8 tracks (audio file, MIDI file and instrument each) plus a master summing bus,
   with trim, polarity, pan, fader and meters, and Mix Down to Editor. **Built.**
2. The window and the strips, keyboard first.
3. Inserts, using the Master Section's plugins, then the two sends and the Reverb and
   Delay returns. **Inserts built** (2026-10-24).
4. Thumbnail displays.
5. Live inputs, record and monitoring (built 2026-10-25: [recording.md](recording.md)).
6. Instruments with MIDI keyboard input. **Built with step 1** (the unified track needs them).
7. The spatial panner and surround or ambisonic masters.

## Built so far (step 1, 2026-10-21)

- `Source/mixer/MidiFile.*` (plain C++): standard MIDI files, types 0, 1 and 2 (and RIFF .rmi),
  running status, SMPTE time, the tempo map; note-on at velocity 0 is a note-off; at the same tick
  note-offs come first. A file cut short keeps what was read.
- `Source/mixer/Instruments.*` (plain C++): the instrument interface (block of events with
  sample offsets, stereo out), the Synth (up to 64 voices, sine/triangle/saw/square with PolyBLEP,
  a two-pole low-pass opened by velocity, ADSR, CC7, CC10, CC11, CC64, pitch bend ±2, and channel
  10 as a General MIDI kit made from sweeps and noise: kick, snare, clap, hats with the open one
  choked by the closed, toms, crash, ride), and track Sampler and Slicer wrappers.
- `Source/mixer/MixerEngine.*` (plain C++): up to 8 tracks and the master; the MIDI clip is
  placed at frames once (again when the tempo changes) and played sample-accurately; notes held
  when the transport stops, jumps or loops, or a channel is turned off, are let go; the
  keyboard plays the armed track; gains ramp over each block; mute and console solo; meters;
  loop; stops at the end; mixdown from the start with up to 10 s of tails (ends after 0.25 s of
  silence), while live playback is silent.
- `Source/engine/PluginInstrument.cpp`: instrument plugins from the scanned list, stereo out
  and MIDI in, their own windows (`Source/engine/PluginWindow.h`, now shared with the Master
  Section), settings saved in the mix; a plugin that throws is not called again.
- `Source/ui/MixerWindow.*`, `MixerSection.cpp`: the window, the strips and master, the Mixer
  menu (`F3`), keys, project files (`.phi`, docs/project.md: the mix's JSON with the patchbay
  and the view; `.phinyxmix` mixes from before still open) and the autosaved project
  `last.phi` in `~/.config/phi/mixer/`. The mixer is heard through the engine's instrument slot with
  the Sampler and Slicer, so it sounds on top of the editor; MIDI goes to the mixer while its
  window was used last.
- Bounce (`B`, `Shift+B`, or the strip's right-click menu): `Engine::mixdown` with one track
  renders it alone, from the start of the mix to its own end plus the instrument's tail, after
  trim and polarity but before fader and pan, whatever mute and solo say. The WAV (32-bit float)
  goes in `<mix> Bounces/` beside the mix (`~/Music/PHI Bounces/` until it is saved).
  In place, the WAV becomes the track's audio, the MIDI file is turned off but kept with the
  instrument, and trim and polarity reset since they are in the WAV. To a new track, the new
  track takes the fader, pan and colour and the original is muted; with 8 tracks it says to
  bounce in place or remove a track.
- The tracks across time (`TrackLanes` in `MixerWindow.cpp`): above the strips, in a splitter,
  one horizontal row per track in its colour, with each clip's waveform (peaks per 256
  frames, made once per file) or MIDI notes, overlapping clips stacked in layers; a clip that
  is muted or silenced by its track's mute or another track's solo is drawn grey, and so are
  notes on channels turned off. Bars from the tempo box, the loop, the
  playhead; click or drag to move it, `Ctrl`+wheel zooms around the pointer, the wheel scrolls,
  a double-click on the ruler fits the mix, and while zoomed in the view follows the playhead.
  Envelope lanes (docs: automation design) will fold out under these rows.
- Clips (step 1b): `mixer::Clip` (an audio or MIDI file, its start, offset into the file,
  length and mute) in `Engine::setClips`; MIDI clips keep a cursor each, notes are owned by the
  clip that played them and let go at its end; audio clips get a 2 ms fade at each edge; stereo
  clips and the instrument are balanced, mono clips panned with equal power. The window's
  `TrackLanes` draws, drags (move, Ctrl copy, between tracks, grid snap), trims, splits,
  duplicates, mutes and deletes them, with undo; `FileBrowser` (a `QFileSystemModel` of folders,
  audio and MIDI files) places files at the playhead; F6 and Tab go round browser, rows and
  strips. Mix files are version 2 (`clips` per track, in seconds); version 1 files open as one
  clip per file.
- Reverse, clip editing and the strip (2026-10-22): `Clip::reversed` plays audio backwards
  (offset and length count in the reversed file, so trims stay on the part you hear) and a MIDI
  clip through `reverseMidi` (each note mirrored, set-up events at the start kept first); `R`
  in the rows reverses the selected clips and `R` on a strip still arms it. A double-click on an
  audio clip hands the part you hear to a new editor tab (named after the clip) and hides the
  mixer, which keeps playing; `F3` saves a changed tab as a 32-bit WAV in `<mix> Edits/`
  (`~/Music/PHI Edits/` until the mix is saved), puts it on the clip with undo, closes the tab
  and shows the mixer. The window opens over the editor's central area, between its side
  panels. Faders are taller, with dB marks (+6 to −60 and −∞) on the left, and meters have dBFS
  marks on the right; trim and pan use the editor's `Potentiometer` knob
  (`Source/ui/Potentiometer.h`, shared with the plugin chain).
- Mix from a Selector set (2026-10-22): Mixer > New Mix from Selector Set makes 8 tracks named
  and coloured from the Find panel's Selector set, from the bottom up (Live Performance: DR, BA,
  PR, LD, VOX, ARP, PD, FX), so each track matches the favourites of its colour.
- Tests: the MIDI reader (tempo map, ordering, damaged files) and the engine (pan law, fader,
  polarity, mute and solo, sample-accurate MIDI, transpose, channels, notes let go on a jump,
  the keyboard, meters, mixdown, serial clips, reversed audio and MIDI) and the Synth.

- The strip and inserts (step 3, 2026-10-24; owner: "a combo of the best features of Ableton
  and Cubase... I like the insert slots of Cubase"; smaller pots, trim and pan side by side with
  their values below). Each track goes through trim, polarity, low cut, high cut, its inserts,
  width, pan and fader. `mixer::Strip` has the low and high cut (Butterworth, 12 or 24 dB an
  octave, 20 Hz to 20 kHz, off by default), the track delay (±500 ms; the track's clips are read
  that much earlier or later, so nothing is buffered and a negative delay works too) and the width
  (0 to 200 %, mid/side, none on a track of mono files). `Engine::setInserts` gives a track up to 8
  effects (`mixer::Effect`, `Source/mixer/Effect.h`, now shared with the patchbay's FX buses and
  made by `AudioEngine::loadEffectPlugin`: built-in, Φ, stock, VST3, LV2, LADSPA, CLAP). A track
  with no cuts, inserts or width keeps the gain-only path; otherwise mono files go on both sides,
  through the cuts and inserts, and the pan after them stays equal power for a track of mono
  files. Bounces have the cuts, inserts and width (before fader and pan); bouncing in place turns
  them off (the inserts stay, bypassed) and resets the delay. Mute comes after the inserts.
  The strip: Trim, Pan and Width pots (30 px) side by side with their values below; the Pre
  section has ØL ØR, the LC and HC buttons (click: on or off; right-click: frequency and slope)
  and Delay. Insert slots are always shown, Cubase style: at least 4, one empty after the last
  used, up to 8; click an empty slot (or press Insert) for the effects menu, by format;
  double-click opens the plugin's window or, for the built-in and Φ effects, a dialog of their
  knobs; right-click bypasses, moves, replaces or removes; drag to reorder. The Strips button
  shows or hides the Instrument, Pre, Inserts and Trim/Pan/Width sections on every strip and
  makes the strips wide. Projects keep all of it (`lowCut`, `highCut`, `delayMs`, `width` and
  `inserts` with each effect's id, state and bypass). The Mix Assistant's Apply EQ Cuts puts its
  overlap cuts in a Φ EQ in the hiding track's inserts, on the nearest band.

- Sends and returns (2026-10-24, owner: "how sends are structured in Ableton (super easy and
  simple)"). `Strip::sendDb` and `sendPre` for A and B: post-fader (and pan) by default, or
  after the inserts and width but before the fader; a muted track's sends are silent and bounces
  have none. `Engine::setReturnEffect` and `returnStrip`: each return runs its effect on the sum
  of its sends, then balance pan, fader and mute, into the master (with the patchbay, into its
  Mix Bus); meters 9 and 10. The return strips sit between the tracks and the master: A Reverb
  starts with Φ Space (1.8 s, fully wet), B Delay with Φ Echo (one tap, a dotted eighth at the
  tempo, 30 % feedback, fully wet); right-click the effect to pick another. Send A and Send B
  pots on every strip show their level in dB (right-click: pre-fader).
- Cubase touches (from the owner's Chris Selim and Dom Sigalas videos): Q-Link (Ctrl+click
  picks strips; with Q-Link on, a change to one goes to all of them, levels by the same amount);
  Alt+Shift while changing a control sends it to every track; Bypass Inserts and Bypass Sends
  for the whole mix (`Engine::bypassInserts`, `bypassSends`); meters coloured from -12 dB and
  again from -6 dB.

- Track modes (2026-10-24, owner: "audio only, instrument only, audio and instrument mixed into a
  single wave form", and a kick sidechained into a drum loop on the same track). `Strip::mode`:
  Hybrid (the default: the audio clips and the instrument as two layers, each with its own level,
  `audioLayerDb` and `instrumentLayerDb`, the Audio and Inst pots), Audio (clips only) or
  Instrument (no audio clips). Hybrid gives 8 tracks 16 sources. The Duck pot (0 to 48 dB) is a
  peak-following sidechain: at -6 dBFS and above the key pushes the ducked sound down by the
  whole amount, below that by its share. The key is one layer of a hybrid (the instrument ducks
  the audio, or the audio the instrument) or another track (`duckKey`; it ducks all of this
  one). Right-click the Duck pot for the key, attack and release, Only Below (`duckBelowHz`: a
  12 dB/oct split, so only the lows duck and the highs pass, e.g. a loop's hats under a kick),
  and three presets from the owner's Ableton video's ducking groups: Hard (24 dB, 120 ms
  release), Soft (8 dB, 220 ms) and HF-Safe (12 dB below 150 Hz). Its label reads key→dB, e.g.
  `I→12` or `3→24↓`. Clipping: the layers are summed in float before trim and the fader, so
  nothing clips inside the strip; the meter's red zone shows a hot track and the Trim brings it
  down.
- Insert Mix: each insert has a dry/wet Mix (`Insert::mix`, right-click a slot: Mix...), for
  parallel compression or saturation in one slot; a slot under 100 % shows its percentage.

- Layout (owner, 2026-10-25): every strip's name is at the bottom, under the fader's value, in
  white on the track's colour. The tracks follow the Selector set chosen in the Find panel, its
  bottom category first (DR on track 1 in Live Performance), in its colours; a new project has a
  track for each of its eight categories, and a track keeps a name or colour the user gave it.
  The Selector's right-click menu makes new sets (New Set..., Delete Set) and changes any
  category's colour for that set (Colour of...), saved in find.json with the names. The MIDI
  inputs moved to the top bar's MIDI... button (its tooltip lists them); messages show for five
  seconds and give their room back. The Device Chain has no title bar.

Next (owner, 2026-10-24): a
Channel window (Pre and a Φ EQ with another track's spectrum to compare), loudness on the master,
snapshots and mixer history, then In/Out routing (Ableton's Audio From and Audio To), live inputs
and recording, thumbnails, templates and the spatial panner. The Inspector (owner, 2026-10-25) is a column at the mixer's
left in both views with the selected track's mode, instrument, Pre, inserts, sends and width; the
strips stay lean (four insert slots, sends, trim and pan, fader, name) in both views.

Device Chain and drag and drop (owner, 2026-10-25, "very close to Ableton's workflow"):
- The mixer has its own Device Chain along its bottom, the whole width, in both views. It shows
  the selected track: its instrument first on the left (Edit opens it, ▾ picks another), then its
  inserts in the order they play, each with ⏻ (on or bypassed), Edit (its own window), up to
  eight knobs from the effect and its Dry/Wet, five to a row. Right-click a card for the insert
  menu (move, replace, remove). Add effect ends the row.
- Clicking a clip on the tracks or in the launcher draws it there instead, read-only: an audio
  clip's waveform (double-click to edit it in the editor) or a MIDI clip's notes on a piano roll,
  in the track's colour. Clicking a track's strip, its name or an empty slot brings the chain back.
- It stays fixed along the bottom, the same height in both views, and is see-through when PHI is.
  While the mixer is in the editor's place the editor's own Device Chain (the master's) steps
  aside, so it is never shown twice, and comes back with the editor.
- Shift+Tab with the pointer over it switches between the Device Chain and the clip last picked
  on that track. A MIDI clip there has its own Φ button (top right), the same as the Inspector's.
- A clip on a hybrid track's timeline is drawn with the clip of the other kind that overlaps it
  most: the audio on the left half, the MIDI on the right.
- The browser's places list ends with Instruments (Synth, Sampler, Slicer and the instrument
  plugins found) and Audio Effects (built-in, stock, then each plugin format). Drag one onto a
  track's strip, the Inspector or the Device Chain: an instrument takes the instrument's place
  (an Audio track becomes Inst, or Hybrid when it has audio clips), an effect joins the end of the
  inserts. Enter or a double-click puts it on the selected track. Backspace goes back to the files.

Fast MIDI edits (owner, 2026-10-25: "MIDI editing, but really fast"; "the idea is speed"):
- No notes are picked or moved by hand. A MIDI clip picked (on the tracks or in the launcher) puts
  a MIDI Clip section at the top of the Inspector, and R reverses the clip.
- **Φ** deals the notes out again along the golden ratio. The pitches, velocities and lengths stay
  the same, and so does the rhythm (the times notes start); which notes start at which of those
  times changes, and a chord stays together. Every press gives another order.
- **Duplicate** makes the clip twice as long, the two copies joined into one clip, so Φ then deals
  across both.
- **½** keeps every other note or chord, and **×2** plays each note twice in its own time, half as
  long. Both can be pressed again.
- **Sa** and **Mode or raga** move every note to the nearest note of a mode (ionian to locrian)
  or one of the major Hindustani ragas: the ten thaats (bilāval, yaman, khamāj, kāfī, āsāvarī,
  bhairavī, bhairav, mārvā, pūrvī, toḍī), then darbārī kānaḍā, jaunpurī, bāgeśrī, bhīmpalāsī,
  deś, bihāg, kedār, miyāṅ kī malhār, megh, bhūpālī, durgā, haṃsadhvani, mālkauns, jog, ahīr
  bhairav, lalit, pūriyā dhanāśrī, śrī, multānī and cārukeśī. A note between two goes down,
  and drums (channel 10) stay. Ragas that share their notes (darbārī kānaḍā, jaunpurī, āsāvarī) are each
  listed. A raga is taken as the notes it uses up and down; its own movement is not followed.
- Each edit writes a new MIDI file (type 0) next to the mix, in "<mix> Edits", or in Music/PHI
  Edits until the mix is saved. The clip plays it from the start, as it was heard (a reversed
  clip is turned round first). Ctrl+Z undoes an edit on the tracks. The code is in
  mixer/MidiFile.h (phiShuffle, duplicated, halfDensity, doubleDensity, toMode, scales), with
  tests.

Counterpoint (owner, 2026-10-25: an automatic MIDI counterpoint generator, in modal systems
beyond the church modes):
- **Counterpoint...** in the MIDI Clip section writes new voices against the clip's melody. The
  melody is the top note at each onset, each held until the next; a rest of an eighth or more
  ends a phrase, and each phrase begins and cadences on its own.
- The species (Fux, Gradus ad Parnassum): each note of the melody is a bar, and the new voice has
  one note in it (first), two (second), four (third), a note tied over the bar line (fourth,
  suspensions), or a mix of all of these (fifth, florid). 1 to 3 voices, above the melody, below
  it, or around it (above, below, above again). As Fux writes in three and four parts, the nearest
  voice takes the species and the others go note against note, unless "Every voice in the
  species" is on.
- The rules a voice must keep: consonances on the beat; dissonances only as passing notes and
  neighbours on the off-beats or as suspensions prepared by a tie and falling a step; no parallel
  fifths or octaves, nor octaves by contrary motion, between any two voices; unisons only at the
  ends; perfect consonances to begin and end, the bass on the melody's note; no tritone, seventh,
  major sixth or leap over an octave. Rules of taste cost a little: steps before leaps, a large
  leap turned back by step, contrary motion, imperfect consonances in the middle, no hidden fifths
  and octaves, one high point, the last note reached by step. A beam search finds the cheapest
  line, voice by voice (the note-against-note voices and the bass first); if one leaves the next
  nothing that keeps the strict rules, all are written again with another seed. Only when nothing
  fits is a strict rule bent, and the status line says how many.
- The mode: any of the tunings (church modes and hypo-modes, jazz modes, ragas, maqams and
  dastgahs, 2 to 21 equal steps), on a root, or the melody's own (the diatonic mode on its last
  note that holds all its notes). The voices use only the mode's keys, and consonance is judged
  by the cents the tuning plays: a maqam's neutral third or sixth counts as a third or sixth; in
  an equal temperament each key is one step. Church modes raise the note a whole step below a
  cadence's arrival (musica ficta). A raga can carry its āroha and avaroha (the notes used going
  up and coming down); the tunings list has none yet, so a raga uses its notes both ways.
- Variation: another number writes other lines that keep the same rules; the same number writes
  the same lines.
- Each voice is a MIDI file (type 0, the melody's tempo map and channel) next to the mix, and a
  clip on a new track with the Synth, named after the voice, where the melody's clip is (on the
  timeline at its start, or in the same scene of the launcher), in the tuning the melody plays in.
  The mixer's 8 tracks are the limit. The code is in mixer/Counterpoint.h, with tests.
- Live loops (owner, 2026-10-25: "play a melody into a clip with a pre-determined length that can
  be adjusted and the recording immediately stops, loops, and the new melody is generated"): record
  into a slot with **Rec 4 Bars** (or any length) and the Record Options' **After a Take: Play It
  and Add Counterpoint**. The voices are written at once with the Counterpoint dialog's last
  choices (its mode, else the track's tuning, else the melody's own) and loop with it, on the
  tracks the last loop's voices went on. Recording itself is in [recording.md](recording.md).

The left column (owner, 2026-10-25: "a relatively static view of the mixer, clip launcher and
horizontal track view"):
- The Finder and the Inspector share one column, the same width in both views. The Finder shows
  first; clicking a track (its strip, its row, its launcher column or a slot) or picking it with
  1-8 puts the Inspector in its place. Ctrl+F (Edit > Find, also in View) brings the Finder back,
  and again goes back to the Inspector; the Inspector button does the same.
- In the editor's place the Finder is the editor's Find panel (search, Selector, places), and the
  mixer's own browser is not shown; closing the Find panel shows the Inspector. In a window of its
  own the mixer's browser is the Finder (Ctrl+B too).
- With Focus Follows Mouse on (View), pointing at the tracks, the mixer's browser or the strips
  gives them the keyboard, as with the editor's panels.

Between the editor and the mixer (owner, 2026-10-25):
- Mixer > Send to Clip Launcher (Alt+F3) puts the sound in the editor, or its selection, into the
  launcher: saved as a WAV in the Edits folder, into the slot under the launcher's cursor when it
  is empty, else the selected track's first empty slot (a scene is added when all are full).
- Double-clicking an audio clip on the tracks opens it in the editor; Ctrl+S there puts the edit
  back on the clip (Ctrl+Z in the mixer undoes it) and returns to the tracks. F3 still does too.

Paulstretch on an audio clip (owner, 2026-10-25: "a fast way to implement it ... in an audio clip
on a horizontal track or a clip launcher clip"):
- An audio clip picked (on the tracks or in the launcher) puts an Audio Clip section at the top of
  the Inspector: **Paulstretch** and how many times longer. The ratio is the editor's Extreme
  Stretch ratio (`extreme/ratio`), and the other settings are the ones Warp > Extreme Stretch last
  used (`ExtremeSettings.h`), so a sound set up in the editor stretches the same way on a clip.
- The clip is stretched as it plays (its part, reversed if it is), with progress and Cancel, and
  saved as a 32-bit WAV, "<name> x<ratio>", in the same Edits folder. The clip then plays it from
  the start; Ctrl+Z puts a track's clip back. A track clip that grows overlaps the clips after it.

Modal tunings for the PHI synth (owner, 2026-10-25: "a feature PHI synth only ... the tuning
could be applied on the clip level, track level or scene level"):
- The Inspector's **Tuning (PHI synth)** section has a tuning and a root for the track, for the
  scene under the launcher's cursor, and for the clip in the slot under it. A track plays in its
  launched clip's tuning, else that scene's, else its own; with none it is 12-TET. The section says
  which tuning the track plays in now.
- The systems (`Source/mixer/Tunings.h`): 74 Hindustani ragas in just intonation (five-limit
  ratios, with Pythagorean komal svaras where the raga sings them low: darbārī's ga and dha, the other kānaḍās' ga,
  toḍī's and bhairav's re and dha), and each of them again equal tempered; the six church modes and six hypo-modes in Pythagorean tuning
  (a hypo-mode's root is the bottom of its range, a fourth below the final); 13 jazz modes; nine
  maqams and seven dastgahs with quarter steps of 50 cents; and 2 to 21 equal steps to the octave.
  Names are in IAST (ragas), IJMES (maqams) and the Encyclopædia Iranica's romanisation (dastgahs), all in
  lower case (owner); a mix saves the plain ASCII name beside each.
- A scale's degrees play on the nearest keys from the root, a quarter step on the key above (rāst's
  E half-flat on E); the keys between keep their 12-TET pitch. Equal steps run up and down the
  keys from the root.
- The mixer hands the tuning to the track's instrument (`mixer::Instrument::setTuning`) whenever
  it changes. Only the PHI synth retunes, and PHI rāga (docs/raga.md) plays a raga in force by its
  sheet; the Synth, Sampler, Slicer and plugins stay in 12-TET.
  MPE for the PHI synth, and tuning outside MPE synths (a channel and a ±48 bend per note), are
  next.

The Sampler and Slicer windows of the Sample menu
could dock into the mixer as track 1 (the other thread's docking work).

Patchbay (2026-10-22, docs/patchbay.md): while the patchbay plays (it always does live), the
engine hands each track's sound to it instead of summing it, takes what is patched into each
track before trim, pan and fader, and plays the tracks in the patchbay's order. The master fader
is the patchbay's Mix Bus. Mix Down still renders the mixer's own sum.
