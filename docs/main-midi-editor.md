# The main editor's MIDI editor

Status: built 2026-10-30.

## What the owner asked for (2026-10-30)

"I prefer just the MIDI editor below and an instrument that I can choose in the Main editor view. I
like the idea of being able to close the MIDI editor and just work on audio only and the inverse:
close the audio editor and work only on MIDI in the Main editor (i.e. not the mixer multi-track
project view)."

"The MIDI editor in this instance is not associated with a specific track at all. Just the MIDI
editor."

"How about the ability to bounce the MIDI track to audio in the Main editor window? The idea here is
to focus on sound design of a single sound."

## Where it is

The main window has two halves in a split you can drag: the audio editor (the waveform, its overview
and ruler) above, and the MIDI editor below.

- **View > Audio Editor** (`Shift+F2`) closes or opens the top half.
- **View > MIDI Editor** (`Shift+F4`), or the ✕ at the right of the MIDI editor's bar, closes or
  opens the bottom half.
- Closing one while the other is closed opens the other, so the window is never empty.
- Both are kept as they were left, for the next start (`view/audioEditor`, `view/midiEditor`).

The mixer's tracks are not here: they stay in the Mixer view. Mixer > Send to Timeline still puts
the editor's sound onto the mixer's selected track.

## The bar

- **Instrument:** the built-in Synth, or any instrument plugin PHI knows (the stock instruments
  included). It plays as one of the patchbay's synths, so it is heard through the patchbay and the
  MIDI keyboard plays it; with Step input on, the keyboard's notes are stepped into the editor.
  The choice is kept for the next start (`midiEditor/instrument`). **Edit** opens a plugin's own
  window.
- **New**, **Open...** and **Save...** work on a standard MIDI file (.mid). New and Open ask first
  when the notes have changed since they were opened or saved.
- **Bounce to Audio** renders the notes through a copy of the instrument, its settings as they are
  now, from the start to the end of the last note and on until its tail falls below -80 dB (at most
  10 seconds more), at the patchbay's rate. The sound opens in a new tab of the audio editor
  (opening the audio editor if it was closed), unsaved, so nothing already open is changed.

Below the bar is the MIDI editor itself ([midi-editor.md](midi-editor.md)): the piano roll, the
pitch systems, step input, quantise, restyle and the rest, without its Apply and Cancel buttons.
