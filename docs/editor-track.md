# The Editor's Track

Status: built 2026-10-26.

## What the owner asked for (2026-10-26)

"Create the clip recording and editing features of the mixer page for the main editor window. In
other words, Audio-only, MIDI-only, and Audio-MIDI. With all that we discussed and created for the
mixer view (i.e. clip launcher and horizontal view). The main editor view is for a single instance,
a single track in horizontal view, not just for audio editing."

Then:

- "It will have all the generative capabilities of the MIDI, but no formal MIDI editor in the
  traditional sense."
- "An option to 'send to clip launcher' or 'send to timeline' (horizontal view)."
- "The user will not be able to save a mixed/split window in the main editor... it will be mixed
  down through the mastering chain or it can be sent to the main editor."
- "We can generate up to four parts max with the counterpoint generator... give the option to the
  user before the user presses generate counterpoint. That will be in the MIDI view of the main
  editor, or the audio/MIDI view. An audio file can be dragged and dropped into the MIDI-only
  editor."

## Where it is

Under the editor's waveform, in a split you can drag. The ✕ at the right end of its bar closes it;
View > Editor Track or Mixer > Editor Track (`Shift+F4`) shows or hides it, and PHI starts with it
shown or hidden as you last left it. The waveform and all of the editor's audio editing stay as they
were above it.

It shows one track of the project: the one last selected, here or in the mixer. It is the mixer's
own track, so whatever you do here the mixer shows, and the other way round. The track box at the
top picks another.

## The track bar

- **Track:** which track of the project the editor shows.
- **Audio / MIDI / Audio + MIDI:** what the track plays and records. Audio only plays its audio
  clips and records the audio input. MIDI only plays its instrument, from its MIDI clips and the
  keyboard, and records MIDI. Audio + MIDI plays both as two layers, and a clip can hold both (the
  MIDI on top, the audio below), as in the launcher. Choosing MIDI gives a track with no instrument
  the Synth.
- **Instrument ▾ and Edit:** the Synth, a Sampler, a Slicer, PHI rāga or an instrument plugin, and
  its window.
- **● Arm:** the track gets the MIDI keyboard and the audio input.
- **Record** and **into the Launcher / onto the Timeline:** arms the track when it is not, then does
  what the mixer's Record does ([recording.md](recording.md)): into the launcher, the next empty
  slot with the mixer's length, quantise and count-in (or the playing loop's other layer, or an
  overdub); onto the timeline, from the playhead until pressed again. A slot's red dot still records
  into that slot, and a split slot's half dot records that layer.
- **Play**, **■ Track** (stops its launcher clip) and **⟲** (back to its timeline).
- **Launcher:** shows or hides the row of slots.
- **Render to Editor:** the track as it sounds, its MIDI through its instrument and its audio, as
  one sound in a new tab. A track of two kinds is never saved from here as it is: it becomes one
  sound this way (or in Mix Down to Editor, `Ctrl+F3`) and then goes through the Master Section like
  any other sound.

## The launcher row

The track's slots, left to right, drawn and played as in the mixer's grid
([clip-launcher.md](clip-launcher.md)): audio only fills a slot with its waveform, MIDI only with
its notes, both split in half. Click the triangle to launch, double-click an empty slot to choose a
file, drop files on it (a file of the other kind joins the clip as its other layer), right-click for
loop, reverse, quantise, Drums to MIDI, **Send to Timeline** and Empty.

## The timeline

The track's clips across time, as in the mixer: dragged, trimmed at their edges, split (`Ctrl+E`),
duplicated (`Ctrl+D`), muted (`Shift+M`), reversed (`R`), deleted; Ctrl+wheel zooms. Right-click a
clip for **Send to Clip Launcher**. Double-click an audio clip to edit it in a tab of the editor;
`F3` or `Ctrl+S` puts the edit back on the clip.

## The clip

The clip picked in the row or the timeline, drawn large, with what can be done to it beside it.
There is no note editor (owner): the MIDI tools make new MIDI from the clip's.

- **MIDI:** Φ (the notes dealt out again along the golden ratio), Duplicate, ½ and ×2 (density),
  a mode or raga on a root, Reverse, and **Counterpoint**. Before Counterpoint, choose **2, 3 or 4
  parts** (the melody and one to three new voices) and **Every voice in the species**; the
  Counterpoint window opens with them.
- **Audio:** Paulstretch, Drums to MIDI, Reverse, Edit in Editor.
- **Send to Timeline** (a slot's clip, at the playhead; a mixed clip as its two layers starting
  together) or **Send to Clip Launcher** (a timeline clip, into the next empty slot).

## Sending the editor's sound

Mixer > Send to Clip Launcher (`Alt+F3`) puts the editor's sound, or its selection, into the
launcher; Mixer > Send to Timeline (`Alt+Shift+F3`) puts it onto the editor's track at the
playhead. Either is saved as a WAV beside the project.

## Audio on a MIDI-only track

An audio file dropped on the track's slots or timeline while it is MIDI only asks what to do:

- **Drums to MIDI:** the track stays MIDI only. The audio goes in under its drums as MIDI notes (a
  mixed slot, or a MIDI clip beside the audio on the timeline), silent while the track is MIDI only.
- **As Audio:** the track becomes Audio + MIDI and the file goes in as it would anywhere else.

Melody to MIDI is not built yet; when it is, it joins this menu.
