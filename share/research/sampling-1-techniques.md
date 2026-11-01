<!--
Research > Sampling, the techniques: how a sampler works and what samplists do with it, each
with its history and the records that made it known. Where PHI uses a technique, the entry says
so in a sentence; how to use it belongs in the manual.
-->

# Sampling

## How a digital sampler works

A digital sampler measures the sound wave many thousands of times a second and keeps each
measurement as a number. The **sample rate** decides the highest frequency it can hold, a little
under half the rate (the Nyquist limit): 44.1 kHz, the CD's rate, holds everything up to about
20 kHz. The **bit depth** decides how finely each measurement is kept, and so the noise floor:
8 bits gives about 48 dB of range, 12 bits about 72, 16 bits about 96. Early samplers used low
rates and depths because memory was dear, and their sound came from it.

To play a sound at another pitch, a sampler reads through it faster or slower, as tape does: an
octave up reads twice as fast and lasts half as long. Reading between stored samples needs
**interpolation**, a guess at the wave between the points; cheap interpolation and reading too
fast both cause **aliasing**, harsh tones that are not in the original, part of the character of
old samplers.

In PHI, Simplest plays a sound across the keyboard by reading it at the speed of each key,
interpolating with a cubic (Catmull-Rom) curve so transposed notes stay smooth.

- [Sampling (signal processing)](https://en.wikipedia.org/wiki/Sampling_(signal_processing)) and
  [Nyquist–Shannon sampling theorem](https://en.wikipedia.org/wiki/Nyquist%E2%80%93Shannon_sampling_theorem), Wikipedia.
- [Audio bit depth](https://en.wikipedia.org/wiki/Audio_bit_depth) and [Aliasing](https://en.wikipedia.org/wiki/Aliasing), Wikipedia.
- [Sample-rate conversion](https://en.wikipedia.org/wiki/Sample-rate_conversion) and
  [Interpolation](https://en.wikipedia.org/wiki/Interpolation), Wikipedia.

## One-shots and gated samples

The simplest use of a sample is the **one-shot**: a hit plays from start to end whatever the
player does, as drums, effects and stabs do. A **gated** sample instead stops (or fades) when the
key or pad is let go, so the player controls its length. Drum machines and the MPC made one-shots
on pads the basic gesture of beat-making; the orchestra hit and the vocal stab are the classic
one-shots of 1980s pop.

In PHI, Simplest's 1-Shot mode plays the whole sound with a fade in and out, either through to
the end (Trigger) or fading when the key is let go (Gate); each of the Slicer's pads is One shot
or Gate.

- [Sampler (musical instrument)](https://en.wikipedia.org/wiki/Sampler_(musical_instrument)) and
  [Orchestra hit](https://en.wikipedia.org/wiki/Orchestra_hit), Wikipedia.

## Keymaps and multisampling

Played far from its original pitch, a sample sounds wrong: everything in it moves, including the
resonances of the instrument's body (the formants), so a piano note shifted up an octave sounds
small and a voice sounds like a chipmunk. **Multisampling** records the instrument every few
notes and maps each recording to a **key zone**, a range of keys around its **root key**, so no
sample is stretched far. The Mellotron did the extreme version, a tape for every key; the
Kurzweil K250 and the Emulator II made it the norm for pianos and orchestras.

In PHI, Simplest has one sample with a root key and a lowest and highest key that play, the
single zone of a multisample; Fixed pitch plays every key at the root's pitch, as for a drum.

- [Sample-based synthesis](https://en.wikipedia.org/wiki/Sample-based_synthesis) and
  [Formant](https://en.wikipedia.org/wiki/Formant), Wikipedia.
- [Kurzweil Music Systems](https://en.wikipedia.org/wiki/Kurzweil_Music_Systems), Wikipedia.

## Velocity layers and round robins

A real instrument changes colour with how hard it is played, not just level: a soft snare is
darker and shorter than a hard one. **Velocity layers** record each note at several dynamics and
switch between them by how hard the key is struck. **Round robins** record the same note several
times and cycle through them, so repeated notes do not sound like a machine gun. Modern libraries
stack both, with dozens of samples for a single note, and **layering** plays several samples at
once to build a new sound, as producers stack two kicks or a clap over a snare.

In PHI, each Slicer pad holds up to four layers that play together, each with its own level,
tune, start, end, fades and direction; velocity sets the level of Simplest's notes and, if asked,
closes its filter on soft ones. Velocity switching and round robins are not yet in PHI.

- [Dynamics (music)](https://en.wikipedia.org/wiki/Dynamics_(music)) and
  [MIDI](https://en.wikipedia.org/wiki/MIDI), Wikipedia.

## Loops and sustain

An instrument that holds a note longer than its sample needs a **loop**: a part of the sound,
usually in its steady middle, that repeats while the key is held. The Fairlight and Emulator
users spent hours finding loop points that did not click or pulse. Three tools help: placing the
ends at **zero crossings** where the wave passes through zero, a **crossfade** that blends the end
of the loop into its start, and the **alternating** (back and forth, or ping-pong) loop, which
hides the seam by reversing. A **sustain loop** plays while the key is held and then lets the
sound play on to its natural end when it is let go.

In PHI, Simplest's Classic mode loops the marked part while a key is held, Continuous, Alternate
(back and forth), Once, Until release or Alternate until release, with a loop crossfade, and its
loop markers snap to zero crossings.

- [Loop (music)](https://en.wikipedia.org/wiki/Loop_(music)) and [Zero crossing](https://en.wikipedia.org/wiki/Zero_crossing), Wikipedia.
- [Fade (audio engineering)](https://en.wikipedia.org/wiki/Fade_(audio_engineering)) (crossfades), Wikipedia.

## Envelopes, filters and voices

Once a sound is on the keys, a sampler shapes it as a synthesizer does: an **ADSR envelope**
(attack, decay, sustain, release) for its level, a **filter** with resonance that follows the
key or velocity, and **voices** that decide how many notes sound at once. One voice makes it
monophonic, with **glide** (portamento) sliding between notes and **legato** changing pitch
without restarting. The **choke group**, from drum machines, lets one pad silence another, as a
closed hi-hat stops an open one.

In PHI, Simplest has the ADSR envelope, a low, high, band-pass or notch filter (12 or 24 dB an
octave, with drive, key follow and velocity), 1 to 32 voices, glide with Fingered and legato. The
Slicer's pads have attack, release and 16 choke groups.

- [Envelope (music)](https://en.wikipedia.org/wiki/Envelope_(music)) and [Portamento](https://en.wikipedia.org/wiki/Portamento), Wikipedia.
- [Hi-hat](https://en.wikipedia.org/wiki/Hi-hat), Wikipedia.

## Breakbeats

A **break** is the part of a funk or soul record where everything drops out but the drums. Hip
hop's DJs, Kool Herc first in the Bronx around 1973, played two copies of a record to extend the
break for dancers; samplers let producers loop it instead. A few breaks have been sampled
thousands of times: Clyde Stubblefield's drums on James Brown's "Funky Drummer" (1970), Lyn
Collins's "Think (About It)" (1972) and above all the **Amen break**, six seconds from the
Winstons' "Amen, Brother" (1969), which, cut up and sped up, became the basis of jungle and drum
and bass in the early 1990s.

- [Breakbeat](https://en.wikipedia.org/wiki/Breakbeat) and [Amen break](https://en.wikipedia.org/wiki/Amen_break), Wikipedia.
- [Funky Drummer](https://en.wikipedia.org/wiki/Funky_Drummer) and [Jungle music](https://en.wikipedia.org/wiki/Jungle_music), Wikipedia.
- [DJ Kool Herc](https://en.wikipedia.org/wiki/DJ_Kool_Herc), Wikipedia.

## Slicing and chopping

A loop played whole is fixed in tempo and order. **Slicing** cuts it at each hit (its
**transients**), into equal parts or on the beat, and puts each slice on a key or pad, so the
pieces can be played in a new order, at a new tempo, with notes left out or repeated. Producers
on the SP-1200 and MPC did it by hand, a practice called **chopping**; Propellerhead's ReCycle
(1994) did it automatically, so a sliced loop followed any tempo, and jungle producers rebuilt the
Amen break this way. Chopping a phrase of a soul record into single notes or words and replaying
them is the method of much hip hop, from Pete Rock and DJ Premier to J Dilla.

In PHI, the Slicer cuts a sound by transients (with a sensitivity), into 2 to 128 equal parts, on
the file's beat grid or at the editor's markers, and puts the slices on its 128 pads; its step
sequencer then plays them in any order, with lanes of different lengths running against each
other.

- [Sampling (music)](https://en.wikipedia.org/wiki/Sampling_(music)) and
  [Hip hop production](https://en.wikipedia.org/wiki/Hip_hop_production), Wikipedia.
- [Transient (acoustics)](https://en.wikipedia.org/wiki/Transient_(acoustics)) and
  [Reason Studios](https://en.wikipedia.org/wiki/Reason_Studios) (ReCycle), Wikipedia.

## Pitching, speed and lo-fi

Many sampled sounds are changed on purpose by speed and low fidelity. **Pitching up** a soul
vocal makes it high and quick, the "chipmunk soul" of Kanye West's and Just Blaze's productions
in the early 2000s; **pitching down** makes it slow, dark and heavy, as in DJ Screw's chopped and
screwed mixes. Early samplers' low rates and bit depths added grit, and producers sampled 33 rpm
records at 45 rpm to fit more into the SP-1200's few seconds, then tuned them down, which lowered
the rate further. **Reversing** a sample, the oldest tape trick, gives swells that rise into a
beat.

In PHI, Simplest's Quality setting has Vintage (12 bits at 26 kHz, as early samplers) and
Turntable 45 and 78 (a record sampled fast and tuned down); Simplest and each Slicer layer can
play a sound reversed.

- [Audio bit depth](https://en.wikipedia.org/wiki/Audio_bit_depth), Wikipedia.
- [Chopped and screwed](https://en.wikipedia.org/wiki/Chopped_and_screwed) and
  [Lo-fi music](https://en.wikipedia.org/wiki/Lo-fi_music), Wikipedia.
- [Backmasking](https://en.wikipedia.org/wiki/Backmasking) (reversed sound), Wikipedia.

## Time-stretching and warping

Changing speed on a sampler changes pitch with it, as on tape. **Time-stretching** changes length
without pitch, and **pitch-shifting** pitch without length, by cutting the sound into short
overlapping pieces and resynthesizing them (granular methods and the phase vocoder). Early
stretching, in the Akai S1000 and on 1990s jungle and garage records, was audibly grainy and became
a sound in itself. Ableton Live (2001) made it continuous: every clip **warps** to the session's
tempo, with markers that pin beats in the audio to the grid.

In PHI, the Warp menu stretches audio with Rubber Band in modes such as Drums, Melody and Full
Mix; Beats cuts at every hit and replays each piece unstretched, as slicing does, and Texture
overlaps short grains.

- [Audio time stretching and pitch scaling](https://en.wikipedia.org/wiki/Audio_time_stretching_and_pitch_scaling), Wikipedia.
- [Phase vocoder](https://en.wikipedia.org/wiki/Phase_vocoder) and [Ableton Live](https://en.wikipedia.org/wiki/Ableton_Live), Wikipedia.

## Granular sampling

**Granular** techniques break a sample into grains of a few to a hundred milliseconds and play
many of them at once, each at its own position, pitch and pan, turning any recording into clouds,
drones and textures. Iannis Xenakis proposed composing with grains of sound in the late 1950s;
Curtis Roads programmed the first granular synthesis on computer in 1974, and Barry Truax made it
run in real time in 1986. Granular samplers are now common in software and hardware.

In PHI, the Warp menu's Texture mode overlaps short grains, scattered a little; Ambient
(Paulstretch) smears a sound into an evolving wash for extreme stretches.

- [Granular synthesis](https://en.wikipedia.org/wiki/Granular_synthesis), Wikipedia.
- [Iannis Xenakis](https://en.wikipedia.org/wiki/Iannis_Xenakis), [Curtis Roads](https://en.wikipedia.org/wiki/Curtis_Roads)
  and [Barry Truax](https://en.wikipedia.org/wiki/Barry_Truax), Wikipedia.

## Collage and plunderphonics

Some music is made almost entirely of samples, heard as quotations. Brian Eno and David Byrne's
*My Life in the Bush of Ghosts* (1981) set found voices from radio over grooves; Steinski's
"Lessons" (1983) and M|A|R|R|S's "Pump Up the Volume" (1987) were made from dozens of records;
the Bomb Squad built Public Enemy's *It Takes a Nation of Millions to Hold Us Back* (1988) from
layer on layer of them, and the Dust Brothers the Beastie Boys' *Paul's Boutique* (1989). John
Oswald called the art **plunderphonics** (1985). Later landmarks are DJ Shadow's
*Endtroducing.....* (1996), Moby's *Play* (1999), built on Alan Lomax's field recordings, and the
Avalanches' *Since I Left You* (2000), from some 3,500 samples.

- [Plunderphonics](https://en.wikipedia.org/wiki/Plunderphonics) and [John Oswald](https://en.wikipedia.org/wiki/John_Oswald_(composer)), Wikipedia.
- [My Life in the Bush of Ghosts](https://en.wikipedia.org/wiki/My_Life_in_the_Bush_of_Ghosts) and
  [Pump Up the Volume (song)](https://en.wikipedia.org/wiki/Pump_Up_the_Volume_(song)), Wikipedia.
- [Sound collage](https://en.wikipedia.org/wiki/Sound_collage), Wikipedia.

## Resampling and following the editor

**Resampling** records a sampler's (or a whole mix's) output and samples that again, so effects,
chops and stretches become a new raw sound to cut up further; it is a staple of drum and bass and
of sound design. Editors and samplers have long passed sounds back and forth for the same reason:
the waveform editor trims, cleans and processes, the sampler plays.

In PHI, Simplest and the Slicer take the editor's selection (`F7` and `F8`), and with Follow editor
on, every edit made to that sound in the editor reaches them at once.

- [Sampling (music)](https://en.wikipedia.org/wiki/Sampling_(music)) and
  [Sound design](https://en.wikipedia.org/wiki/Sound_design), Wikipedia.

## Sample formats and libraries

A sampler instrument is samples plus a map: which file plays on which keys and velocities, with
which loop, envelope and filter. Hardware formats (Akai's S1000 and S3000 disks, E-mu's) were the
first library standards. **SoundFont** (E-mu and Creative Labs, 1990s) packed samples and map
into one file for PC sound cards; the open **SFZ** format (2006) describes the map as plain text
beside ordinary audio files; Kontakt's format, with scripting, holds most commercial libraries.
Tracker modules (MOD, XM, IT) keep samples, instruments and the song together. A **sample pack**
is the producer's equivalent: folders of drum one-shots and loops at labelled tempos and keys.

In PHI, the Slicer saves a kit as a `.phikit` file, with its sounds in a folder beside it, and the
Tracker reads module files.

- [SoundFont](https://en.wikipedia.org/wiki/SoundFont) and [SFZ (file format)](https://en.wikipedia.org/wiki/SFZ_(file_format)), Wikipedia.
- [Module file](https://en.wikipedia.org/wiki/Module_file), Wikipedia.
