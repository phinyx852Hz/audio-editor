# The MIDI editor

Owner, 2026-10-26: "I have decided to add a MIDI editor ... incorporate the best features of the
Cubase MIDI editor, including step editing. Also, MPE support for all the modes we have discussed."
And: "a 'regular' Western, tempered tuning scale and piano roll. then, when a multi-cultural modal
midi editor based on the tradition we're using. One great feature of MIDIRestyle: drop a 'regular'
MIDI file into a specific non-Western MIDI editor and get a melody made to fit it."

**Edit Notes...** in the Inspector's MIDI Clip section opens it on the clip picked (on the tracks or
in the launcher). **Apply to the Clip** writes the notes as a new MIDI file beside the mix, as the
fast edits do, and Ctrl+Z on the tracks puts the clip back.

## The rows are the system the music is in

- It opens as a piano roll in 12-TET, or in the tuning in force on the clip (the slot's, the
  scene's or the track's).
- **System** picks any of PHI's tunings (church modes and hypo-modes, jazz modes, ragas, maqams and
  dastgahs, Javanese, Balinese and Sundanese laras and pathet, Shona mbira tunings, Chinese lü and
  modes, Japanese and Korean modes, 2 to 21 equal steps), **on** a tonic, or a Scala scale. The rows
  become that system's degrees, a row a degree, repeating at its period: seven rows an octave for
  rāst, five for sléndro, sixty for Jīng Fáng's lü.
- **Rows: By pitch** draws a height for each cent instead, the system's degrees as lines across and
  the twelve keys dotted behind them, so how far a degree sits from the piano shows (after
  Intonara's pitch-native roll and MIDIRestyle's scale wheel).
- **Names: The tradition's** names the rows and notes as the tradition does:
  - ragas in sargam, as PHI rāga's sheets write them (S r R g G m M P d D n N, lower case komal, M
    tīvra ma), a dot above for tār saptak and below for mandra (Ṡ, Ṇ);
  - Javanese and Balinese gamelan by kepatihan numbers (sléndro has no 4), dotted for the octave;
    Sundanese degung as la ti na mi da;
  - Chinese modes by wǔshēng (gōng shāng jué zhǐ yǔ, with qīngjué, biànzhǐ, rùn and biàngōng in
    seven-note modes), Korean modes as kung sang kak ch'i u, gong found from the intervals;
  - maqams and dastgahs as letters with quarter tones (E½♭, B½♭, F½♯), spelled from the natural
    beside them as Arabic notation spells them;
  - letters, with the cents off the nearest name, for the others.
  Letters and cents, degree numbers (1, 2... 1', with a ' for each period up) and cents are the
  other choices.
- A note keeps its pitch when the system changes. One between the rows is drawn between them, and
  the info line says how many cents it is off its degree. **Functions > Snap to the System's
  Degrees** moves notes onto the degrees.
- New families of modes name themselves once their family has a namer (`noteedit::addNamer` in
  mixer/NoteEditor.h); a new mode in mixer/Tunings.cpp takes its family's names, and a mode whose
  degrees have names of their own lists them in `TuningSystem::names`.

## Restyle

**Functions > Restyle into This System...** (after MIDIRestyle, by Aaron Brata Aditama, MIT): every
picked note, or all of them, is found as a degree of the system the melody is in and moved to the
same degree of the system shown, counted from each tonic. A line that rises still rises, whatever
the number of degrees: C major's scale into sléndro climbs 1 2 3 5 6 1̇ 2̇ 3̇. The melody's own
system is found from its notes (the Krumhansl-Kessler key profiles) or picked by hand; **To the
nearest pitch** moves each note to the closest degree instead. The notes it had stay behind as
ghosts (dashed), and **Original** plays the clip as it was, switching while it plays, to compare.

## Editing, as in Cubase's Key Editor

- Tools (1 to 5): **Pick** (click, Shift-click, drag a box; drag a note to move it, its right
  edge to stretch it; Alt moves by the cent), **Draw** (click and drag to the length; Ctrl-click
  or a double-click with Pick does the same), **Erase**, **Split**, **Glue**. Right-click deletes.
- **Snap** and the grid (1/1 to 1/32, triplets), the **Length** of new notes, **Quantise** with a
  strength and swing (Q).
- **Functions**: quantise starts, ends and lengths; legato; fixed lengths; delete overlaps (same
  pitch, or one voice); glue; split at the cursor; velocity (set, compress or expand, add, ramp);
  up or down a degree or a period; snap; restyle; select all or none; delete.
- Keys: Up and Down move the picked notes a degree of the system (Shift a period, Alt a cent), Left
  and Right a grid line (Alt a tick), Delete deletes, Ctrl+A picks all, Ctrl+Z and Ctrl+Shift+Z
  undo and redo, Space plays from the playhead (click the ruler to move it).
- The info line shows the picked notes' place (bar.beat.tick), name, degree, cents off it and
  frequency; their length, cents off the degree and velocity can be typed.
- The lane under the notes: velocity (draw a line over the notes), or modulation, volume, pan,
  expression, sustain, timbre (CC 74), channel pressure or pitch bend on a channel (draw a line;
  right-drag clears).
- The keyboard on the left plays a row when clicked.

## Step input

**Step input** puts notes in at the red cursor, which then moves on by the Length: from the MIDI
keyboard (keys held together make a chord), the keyboard on the left, or the computer's keys. On
the computer, A S D F G H J K L ; ' are the degrees up from the tonic in the **period** set, Q W E
R... the period above and Z X C V... the one below, so a five-note laras uses five keys a period
and a 22-śruti scale twenty-two. Space is a rest, Backspace steps back and deletes what was put in
there, Left and Right move the cursor, Shift (or **Chord**) keeps the cursor still, **Insert**
moves the notes after the cursor on to make room. A MIDI key plays the degree nearest it in a
system of up to twelve degrees (as the track's tuning has it), a degree a key from the tonic in a
larger one.

## Saved as MPE, pitch bend or keys

A note's pitch is kept in cents, so it can be anywhere. What the clip's file holds:

- **MPE** (each note its own pitch): the MPE lower zone, a channel a note, each bent to its pitch,
  the bend range sent to every channel (RPN 0, 48 semitones by default). Every system plays exactly,
  on any MPE synth. The built-in Synth follows it, and so does PHI rāga.
- **Pitch bend by channel** (as MIDIRestyle writes it): notes bent the same share a channel, so a
  seven-note maqam takes a channel or two, for synths without MPE (a bend range of 2 by default).
- **Keys** (the track's tuning plays them): each note on its nearest key, no bends. The
  track's tuning then decides the pitch, as before.
- Drums on channel 10 stay there as keys. More than 15 notes sounding at once share channels, and
  the line under the encoding says how many.
- When a clip with notes bent this way comes back to the editor, each note's bend is read back into
  its pitch. A track with a tuning of its own should be set to 12-TET for a bent clip, or the two
  add up; the editor says so.

## Scala files

PHI reads and writes Scala's .scl scales and .kbm keyboard maps with its own code
(core/Scala.h), from the published format descriptions; the Scala program is not part of PHI.
**Scala > Open a Scale** loads a .scl (and a .kbm of the same name beside it), **Open a Keyboard
Map** another map, and **Save This System as .scl and .kbm** writes the system shown, its tonic on
its key at its own pitch.

## Where it came from

MIDIRestyle (MIT) for restyling by degree, the ghosts, A/B and pitch bend shared by channel;
XenRoll (GPL-3.0) for cent-accurate notes, moving a note by the cent, and MPE; Blue's PianoRoll for
rows that follow a Scala scale; Intonara for the pitch axis; Cubase for the tools, the info line,
the lanes, quantise and step input. Ideas only: no code from any of them is used. Next, as agreed
with the owner: pitch curves drawn on notes (meend, gamaka, glides), PHI rāga's ornaments, a scale
wheel; then a staff view and MusicXML, tuning changes within a clip, and a consonance display.

The code: mixer/NoteEditor.h (the model, plain C++, with tests), core/Scala.h, ui/MidiEditor.h.
