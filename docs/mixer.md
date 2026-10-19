# PHINYX Mixer: design draft

Status: proposal for the owner to review (2026-10-19). Nothing here is built yet.

## What it is

A small mixer in its own window: **up to 8 tracks and a master out (track 9)**. A new mix
starts with 2 tracks; Ctrl+T adds one (up to 8) and Ctrl+Shift+T removes the selected one. It mixes bus stems
from another DAW, or works as an instrument for composing, producing and performing. The
mix goes straight into the PHINYX editor, where the Master Section does the mastering.

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
   - It reuses PHINYX's existing goniometer, spectrum and meter code.
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
     becomes a top-down room (5.1 to 9.1.6) or a sphere (ambisonics), using PHINYX's
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
- **A** starts with PHINYX's built-in reverb (a plate, about 1.8 s long).
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
- **Mix Down to Editor:** renders the mix and opens it as a new file in PHINYX.
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

## Keyboard (PHINYX is keyboard-first)

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

Every control has a screen-reader name and announces its value.

## Left for later, unless the owner wants them now

- Automation (recording fader moves).
- Mix snapshots, A/B (cheap to add: store and recall all strip settings).
- Groups and VCAs. With 8 tracks these matter less.

## Build order

1. Engine: 8 stem players plus a master summing bus, with trim, polarity, pan, fader and
   meters, and Mix Down to Editor.
2. The window and the strips, keyboard first.
3. Inserts, using the Master Section's chain code, then the two sends and the Reverb and
   Delay returns.
4. Thumbnail displays.
5. Live inputs, record and monitoring.
6. Instruments with MIDI keyboard input.
7. The spatial panner and surround or ambisonic masters.
