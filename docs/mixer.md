# PHINYX Mixer: design draft

Status: proposal for the owner to review (2026-10-19). Nothing here is built yet.

## What it is

A small mixer in its own window: **8 tracks and a master out (track 9)**. It mixes bus stems
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
5. **Panner**
   - **Pan pot**, a traditional knob. Mono tracks use an equal-power law (−3 dB in the
     middle; −4.5 and −6 dB laws are options). Stereo tracks use balance.
   - **Width dial** (stereo tracks only), from 0 % (mono) to 200 %. It is hidden unless
     the Panner section is wide.
   - **Spatial button**: opens the Harrison-style spatial panner. With a stereo master it
     shows position and width as a stereo image. With a surround or ambisonic master it
     becomes a top-down room (5.1 to 9.1.6) or a sphere (ambisonics), using PHINYX's
     existing Spatial code.
6. **Mute, Solo, Record**. Solo works like a console: soloing a track mutes the others,
   and any number of tracks can be soloed.
7. **Fader and meter (Audition)**
   - A thin, long-throw fader from −∞ to +6 dB, with a peak and RMS meter beside it.
   - The meter has peak hold and a clip light; clicking the clip light clears it.
   - Underneath: the fader value and the highest peak, as text. Typing on a selected strip
     sets the value exactly.

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

- Sends and return tracks (shared reverb or delay).
- Automation (recording fader moves).
- Mix snapshots, A/B (cheap to add: store and recall all strip settings).
- Groups and VCAs. With 8 tracks these matter less.

## Build order

1. Engine: 8 stem players plus a master summing bus, with trim, polarity, pan, fader and
   meters, and Mix Down to Editor.
2. The window and the strips, keyboard first.
3. Inserts, using the Master Section's chain code.
4. Thumbnail displays.
5. Live inputs, record and monitoring.
6. Instruments with MIDI keyboard input.
7. The spatial panner and surround or ambisonic masters.
