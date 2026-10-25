# Recording in the mixer

Status: built 2026-10-25. The comparison of how Ableton Live, Bitwig, Playtime, Tracktion and the
loopers record into clips is the owner's "Recording into clips: how DAWs compare" doc; this file is
what the code does.

## What the owner asked for (2026-10-25)

"I'd like to make this part of a live looping workflow: play a melody into a clip with a
pre-determined length that can be adjusted and the recording immediately stops, loops, and the new
melody is generated." "We still haven't figured out recording into clips or a horizontal track: and
live looping." "I would like a full transport like the audio editor in the mixer view (for both the
clip launcher and horizontal track views)." "I'd prefer a robust offering of options with the main
method super easy to learn with the options for the advanced performer who can handle the
complexity."

## The main method

1. Arm a track (R, or its strip's arm button). It records the MIDI keyboard when it has an
   instrument, else the audio input.
2. Press **Record** (the red button, or Ctrl+R), or click the red dot in one of the armed track's
   empty slots.
3. In the launcher the take goes into the slot under the cursor (or the next empty one below). It
   starts on the next launch quantise (at once, after any count-in, when the transport is
   stopped), records for the length shown (**Rec 4 Bars** to start), stops by itself on that very
   frame and loops from it.
4. Press Record again while the loop plays to **overdub** into it, and again to stop the overdub.
   One button steps through record, play, overdub and play, as a looper pedal does.

In the tracks across time, Record records onto the armed track from the playhead until Record or
Stop is pressed; the take becomes a clip (audio, MIDI or both) where it began.

Every take is a file beside the mix (`<track> take N.wav` or `.mid`, in the mix's Edits folder), so
the project keeps it like any other clip.

## The transport

One row across the top of the mixer, the same in the launcher and the tracks across time: To Start
(Home), Play/Pause (Space), Stop (again: to the start), To End (End), Record (Ctrl+R), Loop (L);
the position in bars, beats and sixteenths over minutes and seconds (the launch clock while the
launcher plays or records); the tempo; the time signature's beats in a bar; **Click** (C), the
metronome; the count-in (none, 1 or 2 bars, for a take started with the transport stopped); the
launch quantise; the take length; **Capture**; and **Record Options**.

## For the advanced performer

- **Length.** Free (it ends on the next quantise after Record is pressed again, so it is whole
  steps of the quantise long), 1 to 32 bars, or **Like the loops**: as long as the longest clip the
  launcher is playing (free when none plays). A take of no set length has room for 3 minutes of
  audio or 10 of MIDI.
- **After a Take** (Record Options): **Play It** (loop, the default), **Stop the Track**, **Play It
  and Record the Next Slot** (the next take starts on the frame this one ends, into the next empty
  slot, and so on until Record is pressed; a free first take sets the length of the rest), or
  **Play It and Add Counterpoint** (mixer.md: the voices are written and loop with it).
- **Quantise Notes**: as played, or onto 1/4, 1/8, 1/16 or 1/32 notes. A note is moved, and its
  end with it, so its length is kept. A note played up to 0.15 s before a take's first beat goes on
  it.
- **Capture** (Ctrl+Shift+R): the mixer always keeps what the keyboard played on the armed track,
  recording or not. Capture makes a clip of the last phrase (back to a rest of two bars) in the
  track's next empty slot: with the transport playing, whole bars from the bar line it started in
  (1, 2, 4, 8, 16 or 32), launched in time with when it was played; stopped, from its first note.
- **The armed track's input**: auto, the MIDI keyboard, the audio input, or both (a slot holds one
  clip, so a slot takes the keyboard; the timeline takes both). Its **input channels**: the stereo
  pair, or the left or right alone (a mono clip). Its **monitoring**: auto (the input heard through
  the track while it is armed and records audio), always on, or off.
- **Audio Input**: the first input (a microphone or line in), or any PipeWire source: an input, the
  desktop's output or one application. **Input Delay** (10 ms to start): how late the input
  arrives; takes are moved earlier by it, and a slot's last few milliseconds are written as they
  come in after it has started to loop.
- **Loop recording on the timeline.** With Loop on, a take that goes round the loop makes a take
  of each pass; the track keeps the newest and each pass is a file of its own.

## Keys

| Key | Does |
| --- | --- |
| Ctrl+R | Record: in the launcher record, stop, overdub, stop; in the tracks across time start or stop a take |
| Ctrl+Shift+R | Capture |
| C | The metronome |
| Space, Home, End, L | Play/pause, to the start, to the end, loop |
| R | Arm the selected track (it gets the keyboard and records) |

## Engine

`mixer::Engine::record` takes a `RecordRequest` (the track, the slot or the timeline, the length
in beats, MIDI or audio, overdub, what happens after, quantise) and makes all the room the take
needs on the calling thread: the slot's clip with room for its whole length, the Slot it goes in,
and for "record the next slot" the next take's too. Everything else happens on the audio thread, to
the frame: the take waits for its quantise (the audio block is split there), records the keyboard
and the input, ends on its frame, swaps itself into the slot and plays on from that frame, with no
allocation. A take asked for while one records on the same track becomes the next take.
`takeRecorded()` hands the UI each take that ended (and the slots it took the place of, to free);
the UI writes the files and gives the clips their paths and songs, which the audio thread never
reads.

A timeline take streams instead: the audio through a ring of 10 seconds that the UI empties every
tick, the notes and the passes of a loop as marks; `takeRecorded()` makes the clips when it ends.

The audio input is a `mixer::Input` read on the audio thread each block; the mixer's is PipeWire's
capture (`AudioCapture`), started only while a track records or monitors audio. What has come in
waits in a small FIFO for its block; no more than 2048 frames beyond a block are kept, so the
delay stays the same from take to take.

The metronome clicks on the launch clock while the launcher plays or records, else on the
timeline; `playWithCountIn` clicks for so many bars with the transport stopped, then starts it.
The tests are in tests/CoreTests.cpp (`testRecording`).

## Next

Takes kept per slot to choose from later (comping), punch in and out on the timeline, recording
the launcher's performance onto the timeline, MIDI learn for Record and Capture (a footswitch), and
recording several armed tracks at once.
