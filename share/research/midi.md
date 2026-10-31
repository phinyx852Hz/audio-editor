<!--
Research > MIDI (owner, 2026-10-31: "same treatment for MIDI as everything else in the Research
section"): what MIDI is, its history from the Universal Synthesizer Interface of 1981 to MIDI 2.0,
its messages, timing, files, sound sets, expression and tuning, controllers and connections, and
what PHI does with each. The sources are the MIDI Association's specifications (midi.org), the
Wikipedia articles linked, and for PHI docs/midi-editor.md, docs/main-midi-editor.md,
docs/control-surfaces.md, docs/foundations.md (section 9) and Source/mixer/MidiFile.h, NoteEditor.h,
MtsEsp.h and core/PhiTuning.h. How to use it belongs in the manual, not here.
-->

# MIDI

## MIDI

MIDI (Musical Instrument Digital Interface) is a standard for musical instruments, computers and
other devices to tell each other what to play. It carries **performance data, not sound**: a key
went down on note 60 at velocity 100, the sustain pedal went down, the modulation wheel moved to
64, change to sound number 5. The receiving synthesizer, sampler or program makes the sound. A
minute of MIDI is a few kilobytes; the same minute of CD audio is ten megabytes.

Three things make up the standard: a **protocol** (the messages and what they mean), a
**transport** (originally a 5-pin DIN cable running at 31,250 bits a second, now also USB,
networks and Bluetooth) and a **file format** (the Standard MIDI File, .mid). Around them grew
agreed sound sets (General MIDI), timing and synchronisation messages, and extensions for
expression and tuning.

MIDI 1.0 was published in 1983 and has been extended ever since without breaking the old devices,
so a 1983 synthesizer and a 2026 program still talk to each other. MIDI 2.0 (2020) is the first
major revision, and it too falls back to MIDI 1.0 when the other end does not speak it.

PHI reads and writes Standard MIDI Files, plays MIDI from any keyboard or controller it finds,
edits MIDI in its MIDI editors, and writes microtonal music as MPE or pitch bend. The entries
below say what it does with each part.

- [MIDI](https://en.wikipedia.org/wiki/MIDI), Wikipedia.
- [The MIDI Association](https://midi.org/), the specifications (free with an account).
- Joseph Rothstein, *MIDI: A Comprehensive Introduction* (A-R Editions, 2nd ed. 1995).
- Paul D. Lehrman and Tim Tully, *MIDI for the Professional* (Amsco, 1993).
- Curtis Roads, *The Computer Music Tutorial* (MIT Press, 1996; 2nd ed. 2023).

## The history of MIDI

Before MIDI, synthesizers were linked by **control voltage and gate** (CV/gate): one voltage for
the pitch (one volt an octave on Moog and Roland, hertz per volt on Korg and Yamaha) and one for
the key being down. It played one note on one instrument, and each maker's voltages differed.
Digital links were proprietary: Oberheim's parallel bus, Roland's DCB (Digital Control Bus, 1981),
Sequential's own.

In 1981 Ikutaro Kakehashi of Roland proposed a common digital interface to Tom Oberheim and to Dave
Smith of Sequential Circuits. Smith and Chet Wood presented the **Universal Synthesizer Interface**
(USI) to the Audio Engineering Society in October 1981; with Roland, Yamaha, Korg and Kawai it
became MIDI. At the NAMM show in January 1983 Smith connected a Sequential Prophet-600 to a Roland
Jupiter-6, and the MIDI 1.0 specification was published in August 1983. Kakehashi and Smith
received a Technical Grammy for it in 2013.

The standard is kept by two bodies together: the MIDI Manufacturers Association (MMA, 1985, since
2016 the MIDI Association) and Japan's AMEI (Association of Musical Electronics Industry, formerly
the Japan MIDI Standards Committee).

The 1980s made MIDI the studio's backbone: the Yamaha DX7 (1983), Roland's MPU-401 interface (1984)
for the IBM PC, the Atari ST (1985) with MIDI ports built in, and sequencers from Steinberg, C-Lab,
Opcode and Mark of the Unicorn. Then came MIDI Time Code (1987), Standard MIDI Files (1988), General
MIDI (1991), Show Control (1991), Machine Control and the Tuning Standard (1992), and Roland GS and
Yamaha XG. USB MIDI (1999) replaced the serial interface on computers, RTP-MIDI (2006) carried MIDI
over networks and Bluetooth LE MIDI (2015) without wires. MPE (2018) gave each note its own
expression, and MIDI 2.0 (2020) a new protocol.

- [MIDI, History](https://en.wikipedia.org/wiki/MIDI#History), Wikipedia.
- [CV/gate](https://en.wikipedia.org/wiki/CV/gate), Wikipedia.
- [Ikutaro Kakehashi](https://en.wikipedia.org/wiki/Ikutaro_Kakehashi), Wikipedia.
- [Dave Smith (engineer)](https://en.wikipedia.org/wiki/Dave_Smith_(engineer)), Wikipedia.
- [Tom Oberheim](https://en.wikipedia.org/wiki/Tom_Oberheim), Wikipedia.
- [Sequential Circuits Prophet-600](https://en.wikipedia.org/wiki/Sequential_Circuits_Prophet-600), Wikipedia.
- [Roland Jupiter-6](https://en.wikipedia.org/wiki/Roland_Jupiter-6), Wikipedia.
- [Yamaha DX7](https://en.wikipedia.org/wiki/Yamaha_DX7), Wikipedia.
- [Roland MPU-401](https://en.wikipedia.org/wiki/Roland_MPU-401), Wikipedia.
- [Atari ST](https://en.wikipedia.org/wiki/Atari_ST), Wikipedia.
- [MIDI Manufacturers Association](https://en.wikipedia.org/wiki/MIDI_Manufacturers_Association), Wikipedia.
- [Association of Musical Electronics Industry](https://en.wikipedia.org/wiki/Association_of_Musical_Electronics_Industry), Wikipedia.
- Dave Smith and Chet Wood, "The 'USI', or Universal Synthesizer Interface", AES convention paper
  (New York, October 1981).

## MIDI 2.0

The MIDI Association and AMEI adopted MIDI 2.0 in February 2020, after showing it at NAMM that
January. It keeps MIDI 1.0's ideas (notes, controllers, channels) and gives them room:

- The **Universal MIDI Packet** (UMP): messages in 32-bit words instead of bytes, in 16 groups of
  16 channels (256 channels on one connection), with a timestamp of its own. A UMP can carry
  MIDI 1.0 messages too.
- **Resolution**: velocity in 16 bits instead of 7, controllers and pitch bend in 32 bits, 16,384
  registered and 16,384 assignable controllers instead of the few NRPN tricks of MIDI 1.0.
- **Per-note controllers and per-note pitch bend**, which do natively what MPE does with channels,
  and attributes on note-on (such as a pitch in 7.9 fixed point, for microtonal music).
- **MIDI-CI** (Capability Inquiry): two devices find out what the other can do, exchange
  **Profiles** (an agreed set of controls for a kind of instrument, such as a drawbar organ) and
  **Property Exchange** (patch lists and settings as JSON).

A revision in June 2023 simplified it (UMP and MIDI 2.0 Protocol 1.1, endpoints and function blocks;
protocol negotiation left MIDI-CI). USB MIDI 2.0 (2020) and Network MIDI 2.0 over UDP (2024) carry
it. Apple's Core MIDI, Android 13, Linux (ALSA, kernel 6.5, 2023) and Microsoft's Windows MIDI
Services support it.

PHI speaks MIDI 1.0 through ALSA; it does not yet use MIDI 2.0 or the UMP.

- [MIDI, MIDI 2.0](https://en.wikipedia.org/wiki/MIDI#MIDI_2.0), Wikipedia.
- [The MIDI Association, MIDI 2.0](https://midi.org/midi-2-0), the specifications.
- [Advanced Linux Sound Architecture](https://en.wikipedia.org/wiki/Advanced_Linux_Sound_Architecture), Wikipedia.

# MIDI / Messages

## Messages, bytes and running status

A MIDI 1.0 message is a **status byte** (its top bit set, 0x80 to 0xFF) followed by one or two
**data bytes** (top bit clear, 0 to 127). That is why so much of MIDI runs from 0 to 127. The
status byte's high half says what the message is and its low half which of the 16 channels:

| Status | Message | Data |
|---|---|---|
| 8n | Note off | note, release velocity |
| 9n | Note on | note, velocity (0 means note off) |
| An | Polyphonic key pressure (aftertouch) | note, pressure |
| Bn | Control change | controller, value |
| Cn | Program change | program |
| Dn | Channel pressure (aftertouch) | pressure |
| En | Pitch bend | 14 bits, low byte first; 8192 is the centre |
| F0 ... F7 | System exclusive | any number of bytes |
| F1, F2, F3, F6 | System common: time code quarter frame, song position, song select, tune request | |
| F8, FA, FB, FC, FE, FF | System real time: clock, start, continue, stop, active sensing, reset | none |

**Running status** saves bandwidth: a message of the same kind and channel as the last one may leave
out its status byte, so a chord is one status byte and pairs of data bytes. That is why note off is
often sent as note on with velocity 0. Real-time messages are a single byte and may arrive between
the bytes of any other message, so clock stays on time.

At 31,250 bits a second, with a start and stop bit for each byte, a three-byte message takes about
a millisecond: a ten-note chord is spread over seven or eight. MIDI 2.0's timestamps and faster
transports remove that limit.

In PHI the MIDI file reader keeps every channel message (notes, controllers, programs, pressure,
pitch bend) and the tempo; other meta events and SysEx are left out (`Source/mixer/MidiFile.h`).

- [MIDI, Messages](https://en.wikipedia.org/wiki/MIDI#Messages), Wikipedia.
- [Byte](https://en.wikipedia.org/wiki/Byte), Wikipedia.

## Channels and modes

One MIDI cable carries **16 channels**, so one keyboard can play 16 sounds on 16 synths (or on one
multitimbral synth) at once. Each device listens to the channels it is set to. Channel 10 is the
drums under General MIDI. With MIDI 2.0's groups a connection has 256.

The original specification defines four **modes**, set by control changes 124 to 127: **Omni**
on or off (listen to every channel, or one), and **Poly** or **Mono** (play chords, or one note a
channel). Mode 4 (omni off, mono), a voice a channel, was used for MIDI guitars, which send each
string on its own channel; MPE grew from it.

PHI's Rhythms, Drums to MIDI and built-in Synth put drums on channel 10, as General MIDI does.

- [MIDI, Channels](https://en.wikipedia.org/wiki/MIDI#Channels), Wikipedia.
- [Multitimbrality](https://en.wikipedia.org/wiki/Multitimbrality), Wikipedia.
- [Guitar synthesizer](https://en.wikipedia.org/wiki/Guitar_synthesizer), Wikipedia.

## Notes and velocity

A **note number** from 0 to 127 is a key: 60 is middle C and 69 the A of 440 Hz, so 128 semitones
from C five octaves below middle C to G above the piano's top. Makers disagree on the octave's
name: Yamaha calls 60 C3, Roland and most others C4. MIDI does not say what pitch a number sounds;
twelve-tone equal temperament is assumed, and a synth's tuning (or a tuning message) can change it.

**Velocity** (1 to 127) is how fast the key went down, which the synth usually maps to loudness and
brightness. A **release velocity** in note off is defined but rarely sent.

In PHI a note's pitch is kept in cents, not as a key, so the MIDI editor can hold any tuning; it is
saved as keys, as MPE or with pitch bend (see Microtonal MIDI).

- [MIDI, Messages](https://en.wikipedia.org/wiki/MIDI#Messages), Wikipedia.
- [Scientific pitch notation](https://en.wikipedia.org/wiki/Scientific_pitch_notation), Wikipedia, for the octave names.
- [Piano key frequencies](https://en.wikipedia.org/wiki/Piano_key_frequencies), Wikipedia.

## Control change

Control change (CC) messages carry the knobs, wheels and pedals: 128 controller numbers, each with
a value from 0 to 127. Some numbers have agreed meanings:

| CC | Meaning | CC | Meaning |
|---|---|---|---|
| 0, 32 | Bank select (coarse, fine) | 64 | Sustain (damper) pedal |
| 1 | Modulation wheel | 65 | Portamento on/off |
| 2 | Breath controller | 66 | Sostenuto |
| 4 | Foot controller | 67 | Soft pedal |
| 5 | Portamento time | 71 to 79 | Sound controllers (74: brightness, MPE's timbre) |
| 6, 38 | Data entry (for RPN and NRPN) | 98 to 101 | NRPN and RPN numbers |
| 7 | Channel volume | 120 | All sound off |
| 10 | Pan | 121 | Reset all controllers |
| 11 | Expression | 123 | All notes off |

Controllers 0 to 31 can be paired with 32 to 63 as the coarse and fine halves of a 14-bit value.
The **RPN** (registered) and **NRPN** (non-registered parameter numbers) select a parameter with
CCs 101 and 100 (or 99 and 98) and set it with data entry: RPN 0 is the pitch bend range, 1 and 2
fine and coarse tuning, 6 the MPE configuration. Many controllers have no agreed meaning, so they can
be learned to anything.

PHI's MIDI Learn maps any CC, note or pitch bend to a control of the mixer or the transport, and the
synths follow RPN 0 for their bend range.

- [MIDI, Controllers](https://en.wikipedia.org/wiki/MIDI#Controllers), Wikipedia.
- [MIDI controller](https://en.wikipedia.org/wiki/MIDI_controller), Wikipedia.
- [Sustain pedal](https://en.wikipedia.org/wiki/Sustain_pedal), Wikipedia.

## Program change and bank select

**Program change** picks one of 128 sounds ("patches") on a channel. As synths grew past 128 sounds,
**bank select** (CC 0 and 32, sent before the program change) picked one of 16,384 banks of 128.
General MIDI fixed what each program number is, so a file plays a piano on any GM synth.

- [MIDI, Messages](https://en.wikipedia.org/wiki/MIDI#Messages), Wikipedia.
- [Synthesizer](https://en.wikipedia.org/wiki/Synthesizer), Wikipedia, for patches.

## Pitch bend and aftertouch

**Pitch bend** is a 14-bit value (0 to 16,383, the centre at 8192) for the whole channel, sent by
the pitch wheel or a strip; how far it bends is the synth's **bend range**, two semitones by default
and set by RPN 0. **Aftertouch** is pressure on a key already down: **channel pressure** one value
for the channel (most keyboards), **polyphonic key pressure** one for each key (rarer: the Yamaha
CS-80 had it before MIDI, the Ensoniq SQ-80 and VFX on MIDI).

Because pitch bend and channel pressure apply to every note on a channel, a keyboard cannot bend
one note of a chord. MPE gets around that with a channel a note.

PHI's synths follow channel and polyphonic pressure and pitch bend; Push's touch strip bends the
pitch in Note mode.

- [Pitch wheel](https://en.wikipedia.org/wiki/Pitch_wheel), Wikipedia.
- [Aftertouch](https://en.wikipedia.org/wiki/Aftertouch), Wikipedia.
- [Yamaha CS-80](https://en.wikipedia.org/wiki/Yamaha_CS-80), Wikipedia.

## System exclusive

A system exclusive (SysEx) message is F0, a manufacturer's ID, any number of data bytes and F7. Makers
use it for their own needs: patch dumps, firmware, putting a controller into a mode, lighting pads.
Some IDs are **universal** (7E non-real-time, 7F real-time) and carry standards: the identity
request, sample dumps, MIDI Time Code full frames, Machine and Show Control, the Tuning Standard,
GM on and off.

PHI's control surfaces use the devices' SysEx (from their makers' programmer's references and
DrivenByMoss) to put Launchpads, Launchkeys and Push into their DAW modes and light them.

- [MIDI, System exclusive messages](https://en.wikipedia.org/wiki/MIDI#System_Exclusive_messages), Wikipedia.

# MIDI / Timing and sync

## MIDI beat clock

**MIDI clock** keeps a drum machine or sequencer in time with another: the leader sends a timing
clock byte (F8) **24 times a quarter note** (24 PPQN), and Start (FA), Stop (FC) and Continue (FB).
The followers count the clocks, so the tempo is whatever rate they arrive at. **Song Position
Pointer** (F2) says where to start, in sixteenth notes (6 clocks each) from the top.

It came from the clock pulses (DIN sync, 24 or 48 pulses a quarter) of Roland's and Korg's drum
machines before MIDI. Its limits are a bar's position only through Song Position, and jitter on a
busy cable; Ableton Link (2016) and MIDI 2.0's timestamps do the job over networks.

PHI keeps its own transport and does not yet send or follow MIDI clock.

- [MIDI beat clock](https://en.wikipedia.org/wiki/MIDI_beat_clock), Wikipedia.
- [DIN sync](https://en.wikipedia.org/wiki/DIN_sync), Wikipedia.

## MIDI Time Code

MIDI Time Code (MTC) carries SMPTE time code (hours, minutes, seconds and frames, at 24, 25, 29.97
drop-frame or 30 frames a second) over MIDI, so sequencers follow tape machines and video. Eight
**quarter-frame** messages (F1) spell one time over two frames; a **full frame** SysEx jumps to a
new time. Chris Meyer and Evan Brooks of Digidesign wrote it in 1986, and it was added to MIDI in
1987.

**MIDI Machine Control** (MMC, 1992) sends a tape machine's transport (play, stop, record, locate)
as SysEx; **MIDI Show Control** (MSC, 1991) cues lighting, sound and stage machinery in theatres.

PHI does not use MTC, MMC or MSC.

- [MIDI timecode](https://en.wikipedia.org/wiki/MIDI_timecode), Wikipedia.
- [SMPTE timecode](https://en.wikipedia.org/wiki/SMPTE_timecode), Wikipedia.
- [MIDI Machine Control](https://en.wikipedia.org/wiki/MIDI_Machine_Control), Wikipedia.
- [MIDI Show Control](https://en.wikipedia.org/wiki/MIDI_Show_Control), Wikipedia.

# MIDI / Files and sounds

## Standard MIDI Files

A Standard MIDI File (SMF, .mid) stores MIDI messages with their times, so any sequencer can open
another's songs. Dave Oppenheim of Opcode designed it; the MMA adopted it in 1988.

The file is a header chunk (MThd) and track chunks (MTrk), the chunk layout of Electronic Arts' IFF.
Each event has a **delta time** (ticks since the last event, as a variable-length number) and the
**division** in the header says how many ticks make a quarter note (or a frame of SMPTE time).
There are three types: **0**, one track with every channel (for simple players and ringtones); **1**,
several tracks played together, the first holding the tempo map (what sequencers write); and **2**,
independent patterns, rarely used.

**Meta events** (FF) carry what is not MIDI: tempo (FF 51, microseconds a quarter note), time and key
signature, track names, lyrics, markers and the end of a track. Microsoft's **RMID** (.rmi) wraps an
SMF in a RIFF file; **.kar** karaoke files are SMFs with lyrics.

PHI reads types 0, 1 and 2 and RMID, and writes type 0 files (the tempo map, markers and channel
events in one track) from the MIDI editors and Rhythms. The MIDI editor keeps a clip's tuning system in a marker event, so the file still plays on any player.

- [MIDI, Standard files](https://en.wikipedia.org/wiki/MIDI#Standard_files), Wikipedia.
- [Variable-length quantity](https://en.wikipedia.org/wiki/Variable-length_quantity), Wikipedia.
- [Interchange File Format](https://en.wikipedia.org/wiki/Interchange_File_Format), Wikipedia.
- [Resource Interchange File Format](https://en.wikipedia.org/wiki/Resource_Interchange_File_Format), Wikipedia, for RMID.
- [Audio file formats](research:Audio%20file%20format), in this menu.

## General MIDI

General MIDI (GM, 1991, by the MMA and the Japan MIDI Standards Committee) made a MIDI file sound
the same on any synth that follows it: **128 programs** in sixteen families of eight (piano,
chromatic percussion, organ, guitar, bass, strings, ensemble, brass, reed, pipe, synth lead, synth
pad, synth effects, ethnic, percussive, sound effects), a **drum kit on channel 10** with fixed keys
(35 and 36 kicks, 38 snare, 42 closed hi-hat, 46 open, 49 crash...), at least 24 voices and 16
channels, and agreed controllers.

Roland's **GS** (1991, the Sound Canvas SC-55) and Yamaha's **XG** (1994, the MU80) added banks of
variations, drum kits and effects, and **General MIDI 2** (1999) folded much of them into the
standard. GM made MIDI the music of computer games and the early web, and of ringtones (SP-MIDI,
Mobile DLS); **Downloadable Sounds** (DLS) let a file bring its own samples.

PHI's built-in Synth plays General MIDI drums on channel 10 from noise and sweeps; Rhythms and Drums
to MIDI write the GM kit's keys and name each drum as GM does.

- [General MIDI](https://en.wikipedia.org/wiki/General_MIDI), Wikipedia.
- [General MIDI Level 2](https://en.wikipedia.org/wiki/General_MIDI_Level_2), Wikipedia.
- [Roland GS](https://en.wikipedia.org/wiki/Roland_GS), Wikipedia.
- [Yamaha XG](https://en.wikipedia.org/wiki/Yamaha_XG), Wikipedia.
- [Roland Sound Canvas](https://en.wikipedia.org/wiki/Roland_Sound_Canvas), Wikipedia.
- [DLS format](https://en.wikipedia.org/wiki/DLS_format), Wikipedia.

## Sequencers and piano rolls

A **sequencer** records, edits and plays back MIDI. The first ones were step sequencers in analog
synths (Moog, Buchla, 1960s) and digital ones such as the Oberheim DS-2 and Roland MC-8 (1977);
with MIDI came hardware sequencers and programs for home computers (Steinberg's Pro-24 and Cubase,
C-Lab's Notator and Creator, Opcode's Vision, MOTU's Performer). Their main views are the **piano
roll** (a grid of pitch against time, after the punched paper rolls of player pianos), the **list
editor** of events, the **drum editor**, the **score** and, in trackers, a pattern of rows.

PHI has two MIDI editors: one under the waveform in the main window, with an instrument of its own
and Bounce to Audio, for one sound at a time; and the mixer's, for clips on tracks. Both are piano
rolls in 12-TET or any of PHI's tunings, with step input, staff and Asian notations, and turning
audio into MIDI (drop an audio file in, or hum or sing a melody). PHI also has a tracker.

- [Music sequencer](https://en.wikipedia.org/wiki/Music_sequencer), Wikipedia.
- [Piano roll](https://en.wikipedia.org/wiki/Piano_roll), Wikipedia.
- [Step sequencer](https://en.wikipedia.org/wiki/Music_sequencer#Step_sequencers), Wikipedia.
- [Roland MC-8 Microcomposer](https://en.wikipedia.org/wiki/Roland_MC-8_Microcomposer), Wikipedia.
- [Tracker](research:Tracker), in this menu.

# MIDI / Expression and tuning

## MPE (MIDI Polyphonic Expression)

MPE gives every note its own pitch bend, pressure and timbre by giving every note its own channel. A
**zone** is a master channel (1 for the lower zone, 16 for the upper) for what applies to all notes,
and member channels (2 upwards, or 15 downwards) handed out a note each. Each note's
expression is three dimensions: **pitch bend** (side to side, ±48 semitones by default on members),
**channel pressure** (pressing in) and **CC 74** (sliding up and down, "timbre"). An RPN 6 message
sets the zone up.

It came from instruments that sense each finger: the Haken Continuum (1990s), the ROLI Seaboard
(2013), Roger Linn's LinnStrument (2014), and before them the mode 4 of MIDI guitars. ROLI, Roger
Linn, Haken Audio, Apple, Bitwig and others proposed it in 2015 (first as Multidimensional
Polyphonic Expression); the MMA adopted it in January 2018. MIDI 2.0's per-note controllers do the
same without the channels.

PHI's built-in Synth and PHI rāga follow MPE, and the MIDI editor saves microtonal clips as MPE, a
channel a note with the bend range set (48 semitones), so they play on any MPE synth.

- [MIDI Polyphonic Expression](https://en.wikipedia.org/wiki/MIDI_Polyphonic_Expression), Wikipedia.
- [Continuum Fingerboard](https://en.wikipedia.org/wiki/Continuum_Fingerboard), Wikipedia.
- [ROLI](https://en.wikipedia.org/wiki/ROLI), Wikipedia.
- [Roger Linn](https://en.wikipedia.org/wiki/Roger_Linn), Wikipedia.
- [The MIDI Association, MPE](https://midi.org/midi-polyphonic-expression-mpe), the specification.

## MIDI Tuning Standard

MIDI 1.0 assumes twelve-tone equal temperament. The **MIDI Tuning Standard** (MTS, 1992) lets a
SysEx message retune a synth: a **bulk dump** of all 128 keys, each as a key and a fraction in steps
of 100/16,384 of a cent (about 0.006 cents), or a **single-note change** while playing; later
**scale/octave** tuning (an offset for each of the twelve notes, repeated every octave) was added.
Few hardware synths followed it outside Yamaha's, Roland's and some microtonal makers'; software
synths more.

**MTS-ESP** (ODDSound, about 2020) takes the idea into plugins: one plugin, the master, tells every
synth that follows MTS-ESP (Surge XT, Vital, Pianoteq and many more) the frequency of every key,
through a shared library instead of MIDI.

PHI can be the MTS-ESP master (Mode > tuning > send the tuning to mts-esp synths), so other synths
play in the tuning in force on the live track.

- [MIDI tuning standard](https://en.wikipedia.org/wiki/MIDI_tuning_standard), Wikipedia.
- [ODDSound, MTS-ESP](https://oddsound.com/mtsespmini.php).
- [Tuning](research:Tuning), in this menu.

## Microtonal MIDI

Music outside twelve-tone equal temperament (maqams with neutral seconds, rāgas with their śrutis,
gamelan laras, just intonation, other equal divisions) has four ways through MIDI:

- **Retune the synth**: the synth's own tuning tables, a Scala file (.scl with .kbm keyboard
  mappings, the format of Manuel Op de Coul's Scala, 1990s), MTS or MTS-ESP. The keys stay keys.
- **Pitch bend by channel**: notes needing the same bend share a channel, which is bent; a seven-note
  maqam takes a channel or two. Any synth can play it.
- **MPE**: a channel a note, each bent to its exact pitch.
- **MIDI 2.0**: a pitch on each note-on, or per-note pitch bend.

PHI keeps every note's pitch in cents, and saves a clip as MPE, as pitch bend by channel or as plain
keys for the track's tuning to play. The MIDI editor's rows can be the degrees of any of PHI's
systems or of a Scala scale, named as each tradition names them.

- [Microtonal music](https://en.wikipedia.org/wiki/Microtonal_music), Wikipedia.
- [Cent (music)](https://en.wikipedia.org/wiki/Cent_(music)), Wikipedia.
- [Equal temperament](https://en.wikipedia.org/wiki/Equal_temperament), Wikipedia.
- [Scala](https://www.huygens-fokker.org/scala/), Manuel Op de Coul's program and its scale archive.
- [Tuning](research:Tuning), [Rāga](research:R%C4%81ga) and [Maqām](research:Maq%C4%81m), in this menu.

# MIDI / Controllers and connections

## MIDI controllers

A MIDI controller sends MIDI and makes no sound itself; the sound comes from a synth or a program.
**Keyboard controllers** (from the Roland MKB-1000 and Sequential's of 1984, now Novation's
Launchkeys, Arturia's, Native Instruments') add knobs, faders and pads. **Pad controllers** grew
from drum machines and Akai's MPC (1988) to grids of lit pads for launching clips: Novation's
Launchpad (2009) and Ableton's Push (2013, Push 2 in 2015, Push 3 in 2023). **Wind controllers**
(Yamaha WX7, 1987; Akai EWI) send breath as CC 2; **keytars** (Roland AX-Synth and AX-Edge, Alesis
Vortex) are keyboards worn like guitars; **electronic drums**, **MIDI guitars** and **control
surfaces** (Mackie Control, for mixing) make up the rest. Grid systems such as Intech Studio's
modules can be any of these.

PHI drives Novation Launchpads and Launchkeys, Ableton Push 2 and 3, the Alesis Vortex Wireless 2,
the Roland AX-Edge and Intech Studio Grid as control surfaces (from DrivenByMoss's device facts),
with Session, Note and Mixer views; any other keyboard plays as a plain MIDI input.

- [MIDI controller](https://en.wikipedia.org/wiki/MIDI_controller), Wikipedia.
- [MIDI keyboard](https://en.wikipedia.org/wiki/MIDI_keyboard), Wikipedia.
- [Novation Launchpad](https://en.wikipedia.org/wiki/Novation_Launchpad), Wikipedia.
- [Akai MPC](https://en.wikipedia.org/wiki/Akai_MPC), Wikipedia.
- [Wind controller](https://en.wikipedia.org/wiki/Wind_controller), Wikipedia.
- [Keytar](https://en.wikipedia.org/wiki/Keytar), Wikipedia.
- [Electronic drum](https://en.wikipedia.org/wiki/Electronic_drum), Wikipedia.
- [DrivenByMoss](https://github.com/git-moss/DrivenByMoss), Jürgen Moßgraber's control surface extensions.

## MIDI Learn

MIDI Learn maps a controller to a parameter by example: pick the parameter, move the knob, and the
program remembers which CC (or note) it sent. It replaced setting controller numbers by hand, and is
in nearly every synth and DAW. A **map mode**, as in Ableton Live, shows every control that can be
learned at once.

PHI's Performance > MIDI Learn and MIDI Map Mode (Ctrl+M) map any CC, note or pitch bend to the
mixer's controls, the launcher and the transport.

- [MIDI controller](https://en.wikipedia.org/wiki/MIDI_controller), Wikipedia.
- [Ableton Live](https://en.wikipedia.org/wiki/Ableton_Live), Wikipedia.

## Cables and connections

MIDI 1.0's cable is a **5-pin DIN** with three ports on a device: **In**, **Out** and **Thru** (a
copy of In, to chain devices). It is a 5 mA current loop at 31,250 bits a second, and the receiver
is **opto-isolated**, so no ground loop hums between instruments. Small devices use **3.5 mm TRS**
jacks with an adaptor (standardised as type A in 2018).

Computers first needed interfaces (the MPU-401, Atari's built-in ports, Mac serial interfaces);
the **USB MIDI** device class (1999) made controllers plug straight in without drivers. **RTP-MIDI**
(RFC 4695, 2006; Apple's network MIDI) carries MIDI over Ethernet and Wi-Fi, **Bluetooth LE MIDI**
(2015) without wires, and **Network MIDI 2.0** (2024) carries the UMP over UDP. Inside a computer,
programs pass MIDI through the system's MIDI service: Core MIDI on macOS, Windows MIDI Services,
and ALSA's sequencer (and JACK or PipeWire) on Linux.

PHI uses ALSA: it lists every MIDI input (Sample > MIDI Inputs), opens new ones as they are plugged
in (it looks every two seconds) and remembers them by name.

- [MIDI, Connectors](https://en.wikipedia.org/wiki/MIDI#Connectors), Wikipedia.
- [DIN connector](https://en.wikipedia.org/wiki/DIN_connector), Wikipedia.
- [Opto-isolator](https://en.wikipedia.org/wiki/Opto-isolator), Wikipedia.
- [RTP-MIDI](https://en.wikipedia.org/wiki/RTP-MIDI), Wikipedia.
- [Bluetooth Low Energy](https://en.wikipedia.org/wiki/Bluetooth_Low_Energy), Wikipedia.
- [Advanced Linux Sound Architecture](https://en.wikipedia.org/wiki/Advanced_Linux_Sound_Architecture), Wikipedia.
- [PipeWire](https://en.wikipedia.org/wiki/PipeWire), Wikipedia.

## Beyond MIDI: OSC

**Open Sound Control** (OSC, 1997, Matt Wright and Adrian Freed at CNMAT, Berkeley) sends named,
typed messages (/mixer/track/1/volume 0.8) over networks, with floating-point values and no channels
or 0-to-127 ranges. It is common in live coding (SuperCollider, Max, Pure Data), installations and
tablet controllers, but never replaced MIDI between instruments.

PHI does not use OSC.

- [Open Sound Control](https://en.wikipedia.org/wiki/Open_Sound_Control), Wikipedia.

# MIDI / In PHI

## MIDI in PHI

Where MIDI is in PHI, with the entry above that explains each part:

- **Two MIDI editors**: in the main window under the waveform (View > MIDI Editor, Shift+F4), with
  its own instrument, New, Open and Save of .mid files and Bounce to Audio; and the mixer's, for MIDI
  clips on tracks (Mixer > Edit MIDI Notes..., Alt+Shift+N). Both are piano rolls in 12-TET or any
  of PHI's tunings or a Scala scale, with step input, staff and Asian notations, and turning audio
  into MIDI. See Sequencers and piano rolls.
- **Files**: Standard MIDI Files of types 0, 1 and 2 and RMID read; type 0 written; the
  tuning kept in a marker. See Standard MIDI Files.
- **Microtonal output**: clips saved as MPE, pitch bend by channel or keys; PHI as an MTS-ESP
  master. See MPE, MIDI Tuning Standard and Microtonal MIDI.
- **Instruments**: the built-in Synth (with GM drums on channel 10) and PHI rāga follow MPE, pitch
  bend, pressure and CC 74; instrument plugins get the MIDI as it is.
- **Drums**: Rhythms and Drums to MIDI write the General MIDI kit on channel 10. See General MIDI.
- **Controllers**: control surfaces for Launchpad, Launchkey, Push, the Vortex and AX-Edge keytars
  and Intech Grid; every other input plays at once; MIDI Learn and Map Mode. See MIDI controllers
  and MIDI Learn.
- **Not yet**: MIDI 2.0, MIDI clock and time code, and OSC.

- [MIDI](https://en.wikipedia.org/wiki/MIDI), Wikipedia.
- docs/midi-editor.md, docs/main-midi-editor.md and docs/control-surfaces.md in PHI's source, for the
  details.
