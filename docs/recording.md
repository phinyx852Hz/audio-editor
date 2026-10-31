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

## The idea (owner, 2026-10-26)

"No MIDI editing. Once a clip has been recorded, it's fixed." "The idea is to live with the mistake
within reason. I was originally not going to have a metronome, but I'll allow that. The performer
who uses this DAW needs to develop their musicianship and learn to get a good take from the start
and not rely on heavy editing." So there is no piano roll: a take is played, kept or recorded
again. Quantise Notes acts as it is played, not afterwards. A quantise for a recorded MIDI clip,
like the one for audio, and making MIDI clips may come.

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

## Layers: audio under MIDI (owner, 2026-10-26)

A clip is audio only, MIDI only, or mixed: an audio layer and a MIDI layer that loop together,
the MIDI playing the track's instrument over the audio, made as it is played rather than later
in post production. A clip of one kind fills its slot with its waveform or its notes; a mixed one
is drawn in two halves, the notes on top and the waveform below (the scenes scroll to keep the
clips tall enough), and so is the clip along the bottom of the mixer.

- A MIDI file dropped on an audio clip in the launcher (or an audio file on a MIDI clip) joins it
  as its other layer, at the loop's length; a file of the same kind takes the slot, as before.
- On the armed track a clip of one kind has a red dot where its other half would be: click it and that half records one pass of the
  loop, starting on the next launch quantise (the slot is launched for it if it is not playing),
  and plays on with the other. Its notes go round the loop from where it began.
- Record (Ctrl+R) while a loop of one layer plays records the other layer, for the track's input
  (the keyboard on a track with an instrument, else audio). On a loop that has the track's layer
  it overdubs that layer, as before.
- The audio half on a track with an instrument records the instrument itself as audio (below), on
  a track set to the audio input that input.
- A layer with no note in it, or cancelled before it began, is taken out again. Each layer is a
  file of its own; the project keeps both (`midiLayer` beside `file` in its slot).
- A track set to Audio or Inst becomes Hybrid when a slot of it gets both, so both are heard;
  the Inspector's layer and Duck dials balance them.
- Φ, counterpoint and anything else PHI does to MIDI act on the MIDI layer only; the audio layer
  keeps the loop's length.

## Drums to MIDI (owner, 2026-10-26)

"Audio drum loops can become MIDI loops for a drum machine." Right-click an audio clip in the
launcher or the tracks across time (or the Inspector's button for the clip shown) and choose
**Drums to MIDI**: each hit becomes a note of the General MIDI kit on channel 10, a kick (36), a
snare (38) or a closed hi-hat (42), as loud as it was played, a 1/16 long. In the launcher the
notes are the clip's MIDI layer, a mixed clip at once; on the timeline a MIDI clip beside the
audio, on the same track. A track with no instrument gets the Synth, whose kit plays them;
choose any other instrument (a drum machine, a sampler) to play them instead. Mute or turn down
either layer with the Inspector's layer dials. The notes are a file of their own and are not
edited afterwards.

How (`Source/mixer/DrumsToMidi.cpp`): hits are found in the whole sound and in its top alone (a
high pass at 5 kHz, so a hat under a ringing kick is found too), 50 ms of silence before so a hit
on the first frame counts. Each is sorted by what it adds over the 43 ms before it: mostly below
150 Hz a kick, mostly above 6 kHz a hat, else a snare. Melody and harmony to MIDI are next.

## Recording the instrument as audio

**Its Instrument, as Audio** (Record Options, the track's Records) records what the track's
instrument plays, from the keyboard and its MIDI clips, as a stereo audio take: before the strip,
so its inserts are heard once when the take plays back through them, and with no input delay.
The take loops and stops by itself like any other.

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
- **The armed track's input**: auto, the MIDI keyboard, the audio input, both (a slot then takes
  the keyboard and the timeline both), or its instrument as audio. Its **input channels**: the stereo
  pair, or the left or right alone (a mono clip). Its **monitoring**: auto (the input heard through
  the track while it is armed and records audio), always on, or off.
- **Audio Input**: the system's default input (as the desktop's sound settings have it; until
  2026-10-30 the first input by name, which could be the wrong device), or any PipeWire source: an input, the
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
A layer (`RecordRequest::layer`) is written into the new half the UI has already put in the slot,
round the loop from its quantise for one pass; an instrument take (`fromInstrument`) reads the
track's instrument as the block renders it instead of the input.
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

Mixed clips on the timeline (a MIDI clip dropped on an audio clip becoming one clip), aligning a
MIDI layer to the audio's beats and transients (owner, 2026-10-26: generative
reshaping, still no MIDI editing), a quantise for a recorded MIDI clip, takes kept per slot to
choose from later (comping), punch in and out on the timeline, recording the launcher's
performance onto the timeline, and recording several armed tracks at once. Controllers (MIDI
learn, Launchpad, Launchkey, Push) have their own work.
