<!--
Research > Editing (owner, 2026-10-31): what audio editing is, how it grew from tape to the
screen, and the terms an editor uses. The editors themselves, compared, are in
editing-1-editors.md. Facts about commercial products are as of October 2026.
-->

# Editing

## Audio editing

Audio editing is changing recorded sound after the fact: cutting out what should not be heard,
joining takes, moving and repeating parts, setting levels and fades, and repairing faults. It
sits between recording and mixing or mastering, and in many jobs (speech, classical music,
restoration, mastering) it is most of the work.

There are two kinds of program for it. A **waveform editor** (also called a two-track or
sample editor) opens one audio file and changes it, one channel set at a time, down to the
single sample; Sound Forge and the audio file window of WaveLab and Audition are the classic
examples. A **multitrack editor** or digital audio workstation lays many clips on many tracks
against a timeline and plays them together, leaving the files untouched. Mastering editors
add a third view, an assembly of finished tracks into an album with gaps, fades, markers and
codes for CD and streaming; WaveLab calls it the Audio Montage, Sequoia the Virtual Project.

PHI is a waveform editor with an album view and a mastering chain, the shape the dedicated
mastering editors share.

- [Audio editing software](https://en.wikipedia.org/wiki/Digital_audio_editor) and
  [Digital audio workstation](https://en.wikipedia.org/wiki/Digital_audio_workstation), Wikipedia.
- [Comparison of digital audio editors](https://en.wikipedia.org/wiki/Comparison_of_digital_audio_editors), Wikipedia.

## From tape to screen

Editing began with magnetic tape in the late 1940s: the editor rocked the reels by hand to find
the edit point, marked the tape, cut it on a splicing block with a razor blade (usually at an
angle, so the join crossfades over a few milliseconds) and joined the ends with splicing tape.
Classical and broadcast editors made hundreds of such splices in a single programme.

Digital editing arrived with the first digital recorders. Soundstream in the United States
(Thomas Stockham, from the late 1970s) edited on a computer screen; Sony's DAE-1100 and DAE-3000 editors,
working on PCM-1610 and 1630 tape, prepared the masters of the first compact discs from 1982.
Sonic Solutions' Sonic System (from 1987, with its NoNOISE restoration) and Digidesign's Sound
Designer and Sound Tools on the Macintosh brought editing to the hard disk in the late 1980s,
with Pro Tools following in 1991. Waveform editors for the personal computer followed in the
1990s: Sound Forge (1992), Samplitude (1992 on the Amiga), Cool Edit, the ancestor of Audition
(mid-1990s), WaveLab (1995) and later Audacity (2000).

- [Magnetic tape sound recording](https://en.wikipedia.org/wiki/Magnetic_tape_sound_recording),
  [Soundstream](https://en.wikipedia.org/wiki/Soundstream) and
  [Sonic Solutions](https://en.wikipedia.org/wiki/Sonic_Solutions), Wikipedia.
- [Pro Tools](https://en.wikipedia.org/wiki/Pro_Tools), Wikipedia.

## Destructive and non-destructive editing

A **destructive** edit changes the audio file itself: after a cut or an effect the samples on
disk are the new ones, and only the undo history (or a backup) brings the old ones back. It is
simple, fast to play back and exact, which is why waveform editors have always worked this way;
Sound Forge and the audio file windows of WaveLab and Audition are destructive, and so was
Audacity until version 4.

A **non-destructive** edit leaves the files alone and stores the change as instructions: this
clip from here to there, faded like this, through these effects. The project plays the
instructions in real time and renders them only on export. Multitrack editors, montages and
album views work this way, so any edit can be changed later. Most editors now offer both,
destructive for surgical work on one file and non-destructive for assembly.

- [Non-linear editing](https://en.wikipedia.org/wiki/Non-linear_editing), Wikipedia.

## Cuts, crossfades and zero crossings

A cut that joins two places in a waveform at different values makes a step, heard as a click.
Editors avoid it in three ways: by snapping the edit point to a **zero crossing**, where the
signal passes through zero; by a short **fade** on each side; or by a **crossfade**, which fades
one side out while the other fades in, the digital form of the angled tape splice. Crossfade
shapes matter: a linear crossfade keeps the amplitude constant for related material (two takes
of the same passage), an equal-power one keeps the loudness constant for unrelated material.

Classical editors work with **source-destination** (three- or four-point) editing: mark a
passage in a take and the place it should go in the master, and the editor drops it in with
crossfades, the method SADiE, Pyramix and Sequoia are built around.

- [Crossfade](https://en.wikipedia.org/wiki/Fade_(audio_engineering)), Wikipedia.
- [Zero crossing](https://en.wikipedia.org/wiki/Zero_crossing), Wikipedia.

## Spectral editing and repair

A spectral editor shows the sound as a spectrogram, time across, frequency up and level as
colour, and lets you select and change a region of it: a cough under a sustained chord, a
phone ringing in a quiet passage, a single click. iZotope RX made it the standard tool for repair
from 2007; Audition, WaveLab, Acoustica Premium and Steinberg's SpectraLayers offer it
too, and Audacity has a basic spectral selection.

Restoration grew alongside: declicking, decrackling and denoising for transfers from disc and
tape, and in the last decade machine-learning tools that separate dialogue from noise or music
into stems.

- [Spectrogram](https://en.wikipedia.org/wiki/Spectrogram) and
  [Audio restoration](https://en.wikipedia.org/wiki/Audio_restoration), Wikipedia.

## Album assembly, CD and DDP

Mastering for release ends in an assembly: the tracks in order, the gaps and crossfades between
them, the start and index markers, and the codes that travel with them (ISRC for each
recording, UPC/EAN for the release, CD-Text). For compact disc the master is delivered as a
**DDP** image (Disc Description Protocol), a set of files the pressing plant reads, rather than
a burned disc; for streaming as files with their metadata.

WaveLab Pro, Sequoia, Pyramix and SADiE write Red Book CD layouts and DDP images; Sound Forge
Pro and Acoustica burn audio CDs from a track layout. Audition had a CD Layout view but removed CD authoring in version
26.3 (June 2026); it never wrote DDP. Audacity has neither.

- [Disc Description Protocol](https://en.wikipedia.org/wiki/Disc_Description_Protocol) and
  [Compact Disc Digital Audio](https://en.wikipedia.org/wiki/Compact_Disc_Digital_Audio), Wikipedia.
- [Mastering](https://en.wikipedia.org/wiki/Mastering_(audio)) and
  [International Standard Recording Code](https://en.wikipedia.org/wiki/International_Standard_Recording_Code), Wikipedia.
- Bob Katz, *Mastering Audio: The Art and the Science* (Focal Press, 3rd ed. 2015).
