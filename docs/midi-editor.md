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

## Hum or sing to notes

**Hum or Sing...** (beside Step input): sing, hum, whistle or play a melody into the audio input
(the mixer's, through PipeWire), or open a recording of one, and its notes are put in from the
step cursor, in the system in force there. The note heard is shown as you sing, by the system's
own names.

- A note starts where the voice settles on a degree and ends where it stops or settles on another.
  A move shorter than **A new note after** (60 ms) is an ornament of the note it is in; a
  note hardly on its own degree is the slide into the next and starts it.
- **On the system's degrees** puts each note on its degree (else at the pitch sung, to the cent).
- **Keep the slides and shakes as pitch curves**: where the voice moved more than 20 cents off
  the note (a mīṇḍ, a gamak, a kaṇ), the note keeps what was sung as its curve; steady singing is
  left steady.
- **Starts and lengths on the grid** quantises them; loudness is the velocity.
- **Play the clip from the cursor while recording** sings along with what is there. The cursor
  moves to the end of each take, so the next carries on; Ctrl+Z takes a take out.
- The pitch is found by YIN (core/Pitch.h, the same tracker as the sound editor's pitch tools),
  from 55 Hz to 1600 Hz: one voice or instrument at a time.

## Tuning changes within a clip

Owner, 2026-10-26: "tuning changes within a piece". A clip can change system part way: a
modulation from rāst to bayātī, a rāga-mālā, sléndro to pélog, a new pathet.

- **Functions > Tuning Changes > Change the System at the Cursor...**: click the ruler where the
  new system starts, choose this, then pick the system from there on in **System**. The change is
  flagged in the ruler with its name.
- The rows show the system in force at the step cursor: click the ruler in another stretch and the
  rows, the names and the System box follow it. The stretches in another system are tinted, with
  their degrees drawn across them, so both are seen at once.
- Picking a system in **System** sets the one in force at the cursor; before the first change it
  is the clip's own.
- A note keeps its pitch when a change is put in; **Snap**, **Up** and **Down** a degree, the
  ornaments and **Restyle** each work in the system of the stretch a note starts in, so restyling a
  12-TET melody over a change moves each half into its own system. A drawn note lands on the
  degrees of its stretch.
- **Take Out the Change in Force at the Cursor** and **Take Out All the Changes** remove them.
- The clip's system and its changes are saved in its MIDI file as markers ("PHI system: ...", its
  name, tonic, period, degrees and names), so the clip opens in them again, even a Scala scale's.
  Saved as MPE or pitch bend, every change sounds as written on any synth; saved as keys, the
  track's single tuning plays them, so MPE is the choice for a clip whose tuning changes.

## The rising spectrum and the scale wheel

- **View: Rising spectrum** (owner, 2026-10-26: "the vertical MIDI view ... with a spectral frequency
  view to show the frequencies rolling up"): frequency across on a log scale, time down the page.
  Each note is drawn at its fundamental, its harmonics (**harmonics**, 1 to 32) fading above it, so
  a chord shows as the comb of partials it puts in the air, in any tuning. The system's degrees are
  the lines, named along the bottom with the frequencies, the twelve keys dotted where they differ.
  While it plays, the notes roll up past the line where they sound. Click a note to pick it; Ctrl
  and the wheel zoom across, Shift and the wheel move across, Alt and the wheel stretch time.
- **Wheel** (after MIDIRestyle's scale wheel and XenRoll's clock diagram): the period as a circle,
  the tonic at the top, each degree at its true angle against the twelve keys' ticks, so rāst's
  neutral third sits between the major and minor ones and yaman's just-intonation ga a little
  under the piano's E. The bars in from the rim show how long each degree sounds in the clip. The
  notes sounding at the playhead (or the step cursor, or the picked notes) are marked at their own
  angles and joined, each line with its interval in cents (the interval clock). Under it, how rough
  they sound together (Sethares' model of Plomp and Levelt's roughness, over each note's
  harmonics), in any tuning.

## Pitch curves and ornaments

Owner, 2026-10-26: "pitch curves for meend, gamaka and glides, plus PHI rāga's ornaments". A note
can carry a pitch curve: points of time into the note and cents off its pitch, straight lines
between. It is drawn over the note in the roll (orange) and bends each partial in the rising
spectrum.

- The **Curve** tool (6): drag across a note and its pitch follows the mouse, on the system's
  degrees (Alt: to the cent); drag back to redraw from there. Right-click clears the curve.
- **Functions > Ornaments**, on the picked notes (or all), in the system shown, each replacing the
  curve a note had:
  - **mīṇḍ** to the next note through the degrees between, each touched a moment as a pulled
    string touches them, or a smooth **glide**; of a chord there, the nearest note is the goal;
  - a **slide in** from the degree below or above, and **kaṇ**, the degree above or below touched
    as the note starts;
  - **āndolan**, a slow swing under the note, **gamak**, a shake to the degree above and back, and
    **vibrato**, timed by the tempo;
  - **murkī** (the note, above, the note, below, the note), **khaṭkā** and **zamzama**;
  - **Clear the Curves**.
- A split note's curve goes with each half; glued notes join theirs.
- Saved, a curved note's bends are sent on its channel while it sounds (one each 48th of a beat, when it changes): with MPE
  and pitch bend by channel it has a channel to itself, so no other note is bent with it; saved as
  keys, on the note's own channel over the track's tuning. A bent clip read back gets its curves
  back. Pitch bend by channel's ±2 semitones limits a curve to two semitones; MPE's ±48 does not.

## Restyle

**Functions > Restyle into This System...** (after MIDIRestyle, by Aaron Brata Aditama, MIT): every
picked note, or all of them, is found as a degree of the system the melody is in and moved to the
same degree of the system shown, counted from each tonic. A line that rises still rises, whatever
the number of degrees: C major's scale into sléndro climbs 1 2 3 5 6 1̇ 2̇ 3̇. The melody's own
system is found from its notes (the Krumhansl-Kessler key profiles) or picked by hand; **To the
nearest pitch** moves each note to the closest degree instead. The notes it had stay behind as
ghosts (dashed), and **Original** plays the clip as it was, switching while it plays, to compare.

## Editing, as in Cubase's Key Editor

- Tools (1 to 6): **Pick** (click, Shift-click, drag a box; drag a note to move it, its right
  edge to stretch it; Alt moves by the cent), **Draw** (click and drag to the length; Ctrl-click
  or a double-click with Pick does the same), **Erase**, **Split**, **Glue**, **Curve** (above). Right-click deletes.
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

## MTS-ESP

**Mode › tuning › send the tuning to mts-esp synths** makes PHI the MTS-ESP tuning master (ODDSound's MIDI
Tuning Standard, External Shared Platform): Surge XT, Vital, Pianoteq and the other synths that
follow MTS-ESP, as plug-ins in PHI or in another program, play every key in the tuning in force
on the live track (its playing clip's, the scene's or its own), as the PHI synth does, and show
its name. A clip saved as keys then plays in its mode on those synths too. It needs ODDSound's
free libMTS library (github.com/ODDSound/MTS-ESP, in /usr/local/lib), which PHI does not
include: when it is missing, turning this on offers **Install libMTS**, which downloads ODDSound's
build for the computer's processor from their repository and puts it in place, asking for the
password (then restart PHI). By hand: `curl -L -o libMTS.so
https://raw.githubusercontent.com/ODDSound/MTS-ESP/main/libMTS/Linux/x86_64/libMTS.so` and
`sudo install -m 755 libMTS.so /usr/local/lib/libMTS.so`; PHI includes only ODDSound's small master code (third_party/MTS-ESP), whose licence lets
anyone use it. Only one program can be the master: PHI says so when another is.

## Staff notation

**Staff** (Ctrl+Shift+N), or **View > Staff notation**, puts the clip's notes on a staff in the
roll's place, and pressing it again brings the roll back. Both views show the same notes, and an
edit in one shows in the other. Time runs across in proportion, as in the roll. Each note's head
sits at its start with a line to its end, and a curved note's line follows its pitch curve up and
down the staff, so a mīṇḍ or a gamak can be seen.

**Notation** chooses what the notes are written in. **The Tradition's Own** (the default) picks
it from the system in force:

| System | Written in |
|---|---|
| Ragas | Sargam in cipher: S r R g G m M P d D n N, with a dot above or below for each octave up or down (Bhatkhande's way) |
| Javanese and Balinese gamelan | Kepatihan numbers in cipher, with dots for the octave |
| Sundanese gamelan | Degree numbers in cipher |
| Chinese modes | Jianpu: 1 to 7 by the semitone above the gong, ♯ or ♭ before, dots for the octave |
| Arabic maqams | The staff, with the half-flat (a struck flat) and the half-sharp, and the three-quarter signs as one glyph |
| Persian dastgahs | The staff, with Vaziri's koron and sori |
| Turkish makams | The staff, in Sagittal's 53-EDO notation |
| Equal divisions | Sagittal's notation for that division when Xen Tuner has one, otherwise ups and downs (generated for any division) |
| Just intonation and Pythagorean modes, Scala scales | HEJI: the comma arrows on sharps and flats, and anything not covered shown in cents |
| 12-TET, jazz modes, mbira tunings | Sharps and flats, with the cents a note is off its key |

Any other notation can be picked from the same menu. The cipher styles are there, and so is PHI's
own staff notation for each tradition. Xen Tuner's whole library is under **Xen Tuner**: HEJI,
HEWM, FJS, Sagittal (Evo and Revo, about a hundred EDOs), ups and downs, Kite, Fokker,
Stein-Zimmermann, chromatic staves and NEJIs. **Open a Xen Tuner Config** reads a config of your
own, written as Xen Tuner's README describes. **Flats Before Sharps** spells a pitch with a flat
wherever a sharp or a flat would both do.

A notation is moved as a whole so that the system's tonic sits on its letter. Sa is a plain C, and
31-EDO's C has nothing over it. A note's accidentals are those of the spelling that comes nearest
for the fewest signs. The cents it is off that spelling are written above it, as in Xen Tuner's
"display cents". Hovering over a note shows its name and its other spellings.

On the staff, a click picks a note (Shift adds to the pick), and a click on empty staff moves the
step cursor. A double-click writes a note on that line or space, without accidentals. **Ctrl+Up**
and **Ctrl+Down** move the picked notes to the next pitch the notation writes (Xen Tuner's up and
down). The editor's own keys work here too: Up and Down by the system's degrees, Delete, Q, and the
arrows. Ctrl and the mouse wheel zoom in time. The clip's markers (for example the sections of an
Architectonics score) are shown along the top. A score from elsewhere in PHI can open straight on
the staff by setting `MidiEditor::Host::staff`.

The notation comes from [Xen Tuner](https://github.com/euwbah/musescore-xen-tuner), euwbah's
microtonal plugin for MuseScore (GPL-3.0-or-later). PHI ports the half of it that reads tuning
configs and works out what each notated pitch sounds (`core/XenTuner.cpp`, under Xen Tuner's own
licence) to plain C++, with no MuseScore. It also runs it the other way round: from a pitch to its
spelling. Xen Tuner's tuning configs ship in `share/xen-tuner/`. The symbols are drawn in Steinberg's
Bravura font (SMuFL, OFL-1.1), which is built into PHI.

### Notations still to come

The view has two kinds, the staff and cipher (`notation::Kind` in `mixer/Notation.h`), each with a
painter of its own in `ui/MidiStaff.cpp`. Another notation is a third kind with its own painter,
plus an entry in `notation::suggested` for the systems it belongs to. The staff, the cipher and
the editing do not change. The owner's source for that work is Walter Kaufmann's *Musical
Notations of the Orient: Notational Systems of Continental East, South, and Central Asia* (Indiana
University Press, 1967; https://archive.org/details/musicalnotations0000kauf). Going by its title
and what is known of its contents, rather than a reading of the scan, it covers notations of
China, Korea, Japan, Mongolia, Tibet and India. Where PHI cannot find an open-source renderer for
one of them, PHI will design its own.

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
the lanes, quantise and step input. Ideas only: no code from any of them is used, apart from ODDSound's
MTS-ESP master library, which XenRoll also uses.

Xen Tuner (GPL-3.0-or-later) for the staff notation: its config format and its parser, ported (see
Staff notation above).

The code: mixer/NoteEditor.h (the model, plain C++, with tests), core/Scala.h, core/XenTuner.h,
mixer/Notation.h (plain C++, with tests), ui/MidiEditor.h, ui/MidiStaff.h.
