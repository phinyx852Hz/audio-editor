<!--
Research > Synthesis > Types: the major methods of synthesis, each with what it is, how it
came about, the instruments that made it known and, where PHI has one, PHI's instrument that
uses it. Roughly from the oldest idea to the newest.
-->

# Synthesis / Types

## Additive synthesis

A sound built by adding sine waves, each with its own frequency and level, and usually its own
envelope. It follows Fourier: any periodic sound is a sum of sines at multiples of its
fundamental, its harmonics, and Helmholtz showed in the 1860s that their balance is much of what
we hear as timbre. In principle it can make any sound; in practice hundreds of partials, each
moving on its own, are a lot to control, which is why it has stayed rarer than subtractive.

The Telharmonium (1906) and the Hammond organ (1935), whose drawbars set the level of each
harmonic, are additive. Bell Labs and the Synclavier (late 1970s) made it digital; the Kawai K5
(1987) and K5000 (1996) brought it to hardware synths, and software such as Native Instruments'
Razor, Image-Line's Harmor and Arturia's Synclavier V to the computer. Resynthesis analyses a
recording into partials and plays them back, changed.

PHI: **Additive** in the Synth menu (Overtonium by Benjamin Dehli).

- [Additive synthesis](https://en.wikipedia.org/wiki/Additive_synthesis), Wikipedia.
- [Harmonic series (music)](https://en.wikipedia.org/wiki/Harmonic_series_(music)), Wikipedia.
- [Fourier series](https://en.wikipedia.org/wiki/Fourier_series), Wikipedia.
- [Hammond organ](https://en.wikipedia.org/wiki/Hammond_organ), Wikipedia.
- [Telharmonium](https://en.wikipedia.org/wiki/Telharmonium), Wikipedia.
- Hermann von Helmholtz, *On the Sensations of Tone* (1863; trans. A. J. Ellis, 1875).

## Subtractive synthesis

The opposite approach: start from a wave rich in harmonics (a sawtooth, a pulse, noise) and take
away with a filter. A low-pass filter whose cutoff opens and closes with an envelope gives the
"wah" of the brass and the pluck of the bass; resonance at the cutoff gives the squelch. The
chain of oscillator, filter and amplifier, each moved by envelopes and LFOs, is the classic
voice of the analogue synthesizer and still the most common way to make a synth patch.

The Novachord (1939) was an early subtractive instrument. Moog's modular systems (from 1964) and
the **Minimoog** (1970), with its four-pole transistor ladder filter, defined it, followed by the
ARP 2600 and Odyssey, the EMS VCS 3, the Yamaha CS-80, the **Prophet-5** (1978, the first fully
programmable polyphonic), the Roland Juno and Jupiter series and the TB-303 bass line. Most
software synths are subtractive at heart.

PHI: the built-in **Synth** (sine, triangle, saw or square through a low-pass filter that opens
with velocity, with an ADSR envelope); **Subtractive1 mono** (CA-72 by Idle Foundry, a Minimoog
Model D) and **Subtractive2 poly** (Helm by Matt Tytel) in the Synth menu; the filter of
Simplest and the Slicer.

- [Subtractive synthesis](https://en.wikipedia.org/wiki/Subtractive_synthesis), Wikipedia.
- [Analog synthesizer](https://en.wikipedia.org/wiki/Analog_synthesizer), Wikipedia.
- [Voltage-controlled filter](https://en.wikipedia.org/wiki/Voltage-controlled_filter), Wikipedia.
- [Minimoog](https://en.wikipedia.org/wiki/Minimoog), Wikipedia.
- [Prophet-5](https://en.wikipedia.org/wiki/Prophet-5), Wikipedia.
- [Roland TB-303](https://en.wikipedia.org/wiki/Roland_TB-303), Wikipedia.

## Modular synthesis

A synthesizer as a set of separate modules (oscillators, filters, amplifiers, envelopes,
sequencers, mixers) joined by patch cables, so the player builds the instrument as well as the
sound. Pitch, gates and every other control travel as voltages (CV/gate), so anything can
modulate anything.

Two schools began at once in the 1960s. Robert Moog's on the East Coast centred on the keyboard
and subtractive filtering; Don Buchla's on the West Coast (the Buchla 100, from 1963) on complex
oscillators, wavefolders and low-pass gates, and no conventional keyboard. Big modular systems
faded in the late 1970s and returned with the Doepfer A-100 (1996), whose **Eurorack** format
now has thousands of modules from hundreds of makers. **VCV Rack** (2017) brought Eurorack to
the computer as open source.

PHI: **Modular** in the Synth menu (Cardinal by DISTRHO, the plugin version of VCV Rack, with its
host modules and VCV Fundamental's oscillators, filter, amplifiers, envelope, LFOs, noise,
mixers, sequencer and scope). PHI's Patchbay routes MIDI and audio between instruments in the
same spirit.

- [Modular synthesizer](https://en.wikipedia.org/wiki/Modular_synthesizer), Wikipedia.
- [Eurorack](https://en.wikipedia.org/wiki/Eurorack), Wikipedia.
- [CV/gate](https://en.wikipedia.org/wiki/CV/gate), Wikipedia.
- [Moog synthesizer](https://en.wikipedia.org/wiki/Moog_synthesizer), Wikipedia.
- [Don Buchla](https://en.wikipedia.org/wiki/Don_Buchla), Wikipedia.
- [VCV Rack](https://en.wikipedia.org/wiki/VCV_Rack), Wikipedia.

## FM synthesis

Frequency modulation: one oscillator (the modulator) changes the frequency of another (the
carrier) at an audio rate, which spreads the carrier into sidebands. Their spacing follows the
ratio of the two frequencies (whole-number ratios give harmonic sounds, others bells and metal)
and their number follows the depth of modulation, the index, so an envelope on the index moves
the brightness as a filter would. A few sine oscillators, called operators, make sounds that
would take hundreds of partials additively. Digital FM is usually phase modulation, which gives
the same sounds and is easier to keep in tune.

John Chowning found it at Stanford in 1967 and published it in 1973; Stanford licensed it to
Yamaha, whose **DX7** (1983), with six operators in 32 algorithms, became the best-selling
synthesizer of its time: the electric piano, bass and bells of the 1980s. Yamaha's OPL and OPN
chips put FM in the Sound Blaster, the Sega Genesis and arcade machines. Native Instruments' FM8
and the open source Dexed carry it on in software.

PHI: **FM** in the Synth menu (OctaSine by Joakim Frostegård, four operators); Tracker2 (Furnace)
emulates Yamaha's FM chips.

- [Frequency modulation synthesis](https://en.wikipedia.org/wiki/Frequency_modulation_synthesis), Wikipedia.
- [Phase modulation](https://en.wikipedia.org/wiki/Phase_modulation), Wikipedia.
- [John Chowning](https://en.wikipedia.org/wiki/John_Chowning), Wikipedia.
- [Yamaha DX7](https://en.wikipedia.org/wiki/Yamaha_DX7), Wikipedia.
- John M. Chowning, "The Synthesis of Complex Audio Spectra by Means of Frequency Modulation",
  *Journal of the Audio Engineering Society* 21 (1973).

## Phase distortion synthesis

A cosine read with its phase bent: the wave runs through part of its cycle faster and the rest
slower, so a sine turns into something like a sawtooth, a square, a pulse or a resonant sweep as
the bend opens. An envelope on the bend (Casio called it DCW, the digitally controlled wave)
plays the part of a filter envelope, without a filter. It is a cousin of phase modulation, with
the phase moved by a fixed shape instead of another oscillator.

Casio introduced it in the **CZ** series (the CZ-101 of 1984, eight models in all), cheap,
polyphonic and digital in the year after the DX7; the VZ series extended it as interactive phase
distortion. Arturia's CZ V and Behringer's CZ-1 Mini revive it.

PHI: the voice heard as PHI starts is a small phase distortion synth with two lines and a new
patch each start, through a granular reverb; PHI's own phase distortion synth, the Φ synth, is to
come.

- [Phase distortion synthesis](https://en.wikipedia.org/wiki/Phase_distortion_synthesis), Wikipedia.
- [Casio CZ synthesizers](https://en.wikipedia.org/wiki/Casio_CZ_synthesizers), Wikipedia.

## Waveshaping and distortion synthesis

A simple wave passed through a curve (a transfer function) that bends it: a sine through a
gentle curve gains a few harmonics, through a steep one many. Chebyshev polynomials as the curve
give exact control of which harmonics appear; a **wavefolder** folds the peaks back on
themselves, the bright, complex tone of the West Coast school. Phase distortion and FM are close
relatives, all grouped as distortion synthesis.

Marc Le Brun and Daniel Arfib described digital waveshaping in 1979; Buchla's 259 oscillator and
the Serge modules made folding an analogue staple, and wavefolders are common Eurorack modules.

- [Distortion synthesis](https://en.wikipedia.org/wiki/Distortion_synthesis), Wikipedia.
- [Waveshaper](https://en.wikipedia.org/wiki/Waveshaper), Wikipedia.
- [Chebyshev polynomials](https://en.wikipedia.org/wiki/Chebyshev_polynomials), Wikipedia.

## Ring and amplitude modulation

Two signals multiplied. **Ring modulation** gives their sum and difference frequencies and
neither of the originals: metallic, clangorous, the voice of the Daleks. **Amplitude
modulation** keeps the carrier too; slow, it is tremolo, fast, it adds sidebands. Both were in
the earliest electronic studios and on most modular systems; Harald Bode built ring modulators
for music in the 1950s, and Stockhausen used them in *Mixtur* and *Mantra*.

- [Ring modulation](https://en.wikipedia.org/wiki/Ring_modulation), Wikipedia.
- [Amplitude modulation](https://en.wikipedia.org/wiki/Amplitude_modulation), Wikipedia.
- [Harald Bode](https://en.wikipedia.org/wiki/Harald_Bode), Wikipedia.

## Sample-based synthesis

A recording as the oscillator: a sample of a real instrument (or anything) played back at
different speeds for different notes, often looped while the key is held and shaped by envelopes
and filters as in subtractive synthesis. Several recordings across the keyboard and the
velocity range (multisampling) keep it natural. A **sampler** lets the player record and edit;
a **ROMpler** plays a fixed library.

Its ancestors are tape: the Chamberlin and the **Mellotron** (1963), with a strip of tape for
every key. Digital sampling arrived with the **Fairlight CMI** and the Synclavier (late 1970s),
then the E-mu Emulator (1981), the Ensoniq Mirage (1984) and the Akai S series and **MPC**
(1988), the heart of hip hop production. The Korg M1 (1988) made ROM samples the workstation
sound; SoundFont and General MIDI put them in every computer; software samplers (Kontakt, and
today's libraries of tens of gigabytes) are the realistic orchestra of film scoring. Module
files and trackers (Amiga, 1987) are sample-based too.

PHI: **Simplest**, after Ableton's Simpler, and the **Slicer**, a drum machine of slices and
layers, in the Sample menu; **Retro Sampler** (Ami by Finley Baker, in the manner of the Amiga's
8-bit sound); **Tracker1** (MilkyTracker) plays XM, MOD, IT and S3M modules.

- [Sample-based synthesis](https://en.wikipedia.org/wiki/Sample-based_synthesis), Wikipedia.
- [Sampler (musical instrument)](https://en.wikipedia.org/wiki/Sampler_(musical_instrument)), Wikipedia.
- [Rompler](https://en.wikipedia.org/wiki/Rompler), Wikipedia.
- [Mellotron](https://en.wikipedia.org/wiki/Mellotron), Wikipedia.
- [Fairlight CMI](https://en.wikipedia.org/wiki/Fairlight_CMI), Wikipedia.
- [Akai MPC](https://en.wikipedia.org/wiki/Akai_MPC), Wikipedia.
- [SoundFont](https://en.wikipedia.org/wiki/SoundFont), Wikipedia.

## Wavetable synthesis

An oscillator that plays one of a table of single-cycle waves, and moves through the table as
the note plays: an envelope or LFO on the position morphs the timbre smoothly from one wave to
the next. (In the 1990s the word was also used, loosely, for the sample playback of PC sound
cards.)

Max Mathews' MUSIC II (1958) already read waves from a table. Wolfgang Palm made the moving
wavetable his own in the **PPG Wave** (1981), and continued it at Waldorf (Microwave, 1989; Wave,
1993). In software it became the sound of electronic dance music: Native Instruments' Massive
(2007), Xfer's **Serum** (2014) and Matt Tytel's **Vital** (2020), which draw their own tables
and import any sound as one.

PHI: **Wavetable** in the Synth menu (Vaporizer2 by VAST Dynamics). Tracker2 (Furnace) emulates
the wavetable chips of the PC Engine and the Game Boy.

- [Wavetable synthesis](https://en.wikipedia.org/wiki/Wavetable_synthesis), Wikipedia.
- [PPG Wave](https://en.wikipedia.org/wiki/PPG_Wave), Wikipedia.
- [Wolfgang Palm](https://en.wikipedia.org/wiki/Wolfgang_Palm), Wikipedia.
- [Waldorf Music](https://en.wikipedia.org/wiki/Waldorf_Music), Wikipedia.

## Vector synthesis and wave sequencing

**Vector synthesis** mixes four sound sources at the corners of a square, the mix set by a
joystick and moved by an envelope that traces a path across it: the Sequential Prophet VS
(1986), designed by Dave Smith's team, then the Yamaha SY22 and the Korg Wavestation. **Wave
sequencing** plays a list of waves one after another, each with its own length, pitch and
crossfade, so the timbre walks through them in rhythm: the Korg **Wavestation** (1990) and, in
2020, the Korg Wavestate.

- [Vector synthesis](https://en.wikipedia.org/wiki/Vector_synthesis), Wikipedia.
- [Korg Wavestation](https://en.wikipedia.org/wiki/Korg_Wavestation), Wikipedia.
- [Dave Smith (engineer)](https://en.wikipedia.org/wiki/Dave_Smith_(engineer)), Wikipedia.

## Linear arithmetic synthesis

Roland's answer to the DX7: a short sample of an attack (the hammer, the breath, the pluck),
where the ear finds most of an instrument's character, joined to a simple synthesized sustain,
with digital filters and chorus and reverb built in. The **Roland D-50** (1987) and the smaller
D-10, D-20 and MT-32 made it the sound of the late 1980s; the MT-32 was the soundtrack of PC
games before General MIDI.

- [Linear arithmetic synthesis](https://en.wikipedia.org/wiki/Linear_arithmetic_synthesis), Wikipedia.
- [Roland D-50](https://en.wikipedia.org/wiki/Roland_D-50), Wikipedia.
- [Roland MT-32](https://en.wikipedia.org/wiki/Roland_MT-32), Wikipedia.

## Granular synthesis

Sound as a cloud of grains: tiny pieces, a few to a hundred milliseconds long, each with its own
window, pitch, position and pan, by the hundred per second. Read from a recording, grains can
stretch it without changing its pitch, freeze a moment, or scatter it into a texture; generated,
they make clouds no other method does.

Dennis Gabor proposed acoustic quanta in 1946; Iannis Xenakis composed with grains of tape in
*Analogique A et B* (1958–59) and theorized them in *Formalized Music*; Curtis Roads programmed
the first digital granular synthesis in 1974–75, and Barry Truax made it run in real time in
1986 (*Riverrun*). Today it is in Ableton's Granulator, Output's Portal, Mutable Instruments'
Clouds module and countless others.

PHI: **Granular** in the Sample menu (GRNLR by Raffael Seyfried); the granular reverb of the
splash voice; Extreme Stretch, which shares granular's long overlapping windows.

- [Granular synthesis](https://en.wikipedia.org/wiki/Granular_synthesis), Wikipedia.
- [Dennis Gabor](https://en.wikipedia.org/wiki/Dennis_Gabor), Wikipedia.
- [Iannis Xenakis](https://en.wikipedia.org/wiki/Iannis_Xenakis), Wikipedia.
- [Curtis Roads](https://en.wikipedia.org/wiki/Curtis_Roads), Wikipedia.
- [Barry Truax](https://en.wikipedia.org/wiki/Barry_Truax), Wikipedia.
- Curtis Roads, *Microsound* (MIT Press, 2001).

## Physical modelling synthesis

Synthesis from the physics of an instrument instead of its sound: a model of the string, the
tube, the reed, the membrane and the body, solved in real time, so the sound responds to how it
is played as a real one does. **Digital waveguides** model a string or a tube as a delay line,
one period long, with its losses in the loop; **modal synthesis** as a set of resonances; finite
difference methods as a mesh of points.

The Kelly–Lochbaum vocal tract (1961) and Hiller and Ruiz's strings (1971) were the first
models. The **Karplus–Strong** algorithm (1983), a burst of noise in a filtered delay loop,
plucks a string with almost no computation; Julius O. Smith at Stanford generalized it into
digital waveguides. Yamaha's VL1 (1994) was the first commercial physical modelling synth,
followed by the Korg Prophecy and Z1; Applied Acoustics Systems, Pianoteq's modelled pianos and
Ableton's Tension and Collision keep it going in software.

PHI: **PHI rāga**, a modelled sitar: each string a waveguide whose length follows the pitch as it
is pulled (mīṇḍ and āndolan bend the string, not a recording), the jawārī bridge's buzz, the
tarab strings ringing in sympathy and two chikārī strings. Process > Effect > Generate > Pluck (Nyquist) is a
Karplus–Strong string.

- [Physical modelling synthesis](https://en.wikipedia.org/wiki/Physical_modelling_synthesis), Wikipedia.
- [Digital waveguide synthesis](https://en.wikipedia.org/wiki/Digital_waveguide_synthesis), Wikipedia.
- [Karplus–Strong string synthesis](https://en.wikipedia.org/wiki/Karplus%E2%80%93Strong_string_synthesis), Wikipedia.
- [Sitar](https://en.wikipedia.org/wiki/Sitar), Wikipedia.
- Julius O. Smith III, *Physical Audio Signal Processing* (W3K Publishing, 2010; online at
  ccrma.stanford.edu).

## Analogue modelling

Digital synthesis that imitates analogue circuits: their oscillators that drift, their ladder
and state-variable filters that saturate, their envelopes' curves. The **Clavia Nord Lead**
(1995) gave it the name virtual analogue; the Access Virus, Roland JP-8000 (its "supersaw") and
Korg MS2000 followed. In software it became the norm: Arturia's V Collection, u-he's Diva and
Repro, and emulations of nearly every classic synth, Moog, ARP, Oberheim, Prophet and Juno.

PHI: Subtractive1 mono (CA-72) models the Minimoog Model D.

- [Analog modeling synthesizer](https://en.wikipedia.org/wiki/Analog_modeling_synthesizer), Wikipedia.
- [Nord Lead](https://en.wikipedia.org/wiki/Nord_Lead), Wikipedia.
- [Access Virus](https://en.wikipedia.org/wiki/Access_Virus), Wikipedia.
- [Arturia](https://en.wikipedia.org/wiki/Arturia), Wikipedia.

## Sound chips and chiptune

The synthesizers inside home computers, game consoles and arcade machines, and the music written
for them. The **programmable sound generators** (General Instrument's AY-3-8910, Texas
Instruments' SN76489, the NES's Ricoh 2A03) give a few square or pulse channels and noise; the
Commodore 64's **SID** (MOS 6581, Bob Yannes, 1982) is a small analogue subtractive synth with a
real filter; Yamaha's FM chips (the OPL2 of the AdLib and Sound Blaster, the YM2612 of the Sega
Genesis) bring four-operator FM; the Game Boy and PC Engine read short wavetables; the Amiga's
Paula plays four channels of 8-bit samples. **Chiptune** is the music made with them, on the
machines or in emulation, and trackers are its usual tool.

PHI: **Tracker2** (Furnace) emulates over 80 sound chips, several at once; **Tracker1**
(MilkyTracker) plays the Amiga's and the PC's module formats.

- [Chiptune](https://en.wikipedia.org/wiki/Chiptune), Wikipedia.
- [Sound chip](https://en.wikipedia.org/wiki/Sound_chip), Wikipedia.
- [Programmable sound generator](https://en.wikipedia.org/wiki/Programmable_sound_generator), Wikipedia.
- [MOS Technology 6581](https://en.wikipedia.org/wiki/MOS_Technology_6581), Wikipedia.
- [Yamaha YM2612](https://en.wikipedia.org/wiki/Yamaha_YM2612), Wikipedia.
- [Music tracker](https://en.wikipedia.org/wiki/Music_tracker), Wikipedia.

## Spectral synthesis

Synthesis in the frequency domain: a sound analysed into its spectrum frame by frame (the
short-time Fourier transform), changed there, and turned back into sound. The **phase vocoder**
(Flanagan and Golden, Bell Labs, 1966) stretches time and shifts pitch independently;
**spectral modelling synthesis** (Xavier Serra, 1989) splits a sound into sinusoids and a noise
residual and changes each. Kyma, IRCAM's AudioSculpt, Image-Line's Harmor and the spectral
displays of modern editors work this way; the Russian **ANS synthesizer** (1958) did it
photo-optically, the composer drawing the spectrum on glass.

PHI: Extreme Stretch keeps each long window's spectrum and randomises its phases (the
Paulstretch algorithm), so a sound becomes an evolving texture at any length; the spectral
repair tools edit the spectrum directly.

- [Phase vocoder](https://en.wikipedia.org/wiki/Phase_vocoder), Wikipedia.
- [Spectral modeling synthesis](https://en.wikipedia.org/wiki/Spectral_modeling_synthesis), Wikipedia.
- [Short-time Fourier transform](https://en.wikipedia.org/wiki/Short-time_Fourier_transform), Wikipedia.
- [ANS synthesizer](https://en.wikipedia.org/wiki/ANS_synthesizer), Wikipedia.
- [Audio time stretching and pitch scaling](https://en.wikipedia.org/wiki/Audio_time_stretching_and_pitch_scaling), Wikipedia.

## Speech, formant and vocoder synthesis

Synthesis of the voice. A **formant** synthesizer filters a buzz (the vocal folds) and a hiss
(breath) through resonances at the formants that make each vowel; **linear predictive coding**
estimates those filters from speech itself. The **vocoder** analyses one sound's spectrum in
bands and imposes it on another, so a synth can talk.

Homer Dudley's vocoder and his Voder (1939) began it at Bell Labs; in 1961 an IBM 704 there sang
"Daisy Bell", which *2001: A Space Odyssey* remembered. Vocoders became the robot voice of
Kraftwerk, Wendy Carlos and Herbie Hancock; Dennis Klatt's formant synthesizer gave Stephen
Hawking his voice; Yamaha's Vocaloid (2004) sings from concatenated recordings.

- [Speech synthesis](https://en.wikipedia.org/wiki/Speech_synthesis), Wikipedia.
- [Formant](https://en.wikipedia.org/wiki/Formant), Wikipedia.
- [Vocoder](https://en.wikipedia.org/wiki/Vocoder), Wikipedia.
- [Voder](https://en.wikipedia.org/wiki/Voder), Wikipedia.
- [Linear predictive coding](https://en.wikipedia.org/wiki/Linear_predictive_coding), Wikipedia.
- [Vocaloid](https://en.wikipedia.org/wiki/Vocaloid), Wikipedia.

## Concatenative synthesis

A sound put together from short units chosen from a large database of recordings, each picked
to match a target: the next phoneme of a sentence, or the pitch, loudness and brightness of a
sound being imitated. Speech synthesis used it first (unit selection); Diemo Schwarz's CataRT at
IRCAM (2006) made it a musical instrument, playing a corpus of sounds from a map of their
features.

- [Concatenative synthesis](https://en.wikipedia.org/wiki/Concatenative_synthesis), Wikipedia.
- [IRCAM](https://en.wikipedia.org/wiki/IRCAM), Wikipedia.

## Scanned synthesis

A slowly moving physical system, a simulated string of masses and springs vibrating below 15 Hz,
too slow to hear, scanned round and round fast enough to become a wave: the scanning rate is the
pitch, the movement of the system the timbre, and the player shapes it by hand. Max Mathews, Bill
Verplank and Rob Shaw devised it at Interval Research in 1998–99; it lives on in Csound and Pure
Data.

- [Scanned synthesis](https://en.wikipedia.org/wiki/Scanned_synthesis), Wikipedia.
- [Max Mathews](https://en.wikipedia.org/wiki/Max_Mathews), Wikipedia.

## Synthesis languages

Synthesis by program: an instrument written as code, from unit generators (an oscillator, a
filter, an envelope) joined in a text instead of with cables, and played by a score or in real
time. Max Mathews' **MUSIC** programs at Bell Labs (from 1957) founded the family, MUSIC-N, and
Barry Vercoe's **Csound** (MIT, 1986) carries it on. Graphical patching came with Miller
Puckette's **Max** (IRCAM, 1988) and **Pure Data** (1996); live coding with James McCartney's
**SuperCollider** (1996) and ChucK; compiling to plugins with Grame's **Faust**. Roger
Dannenberg's **Nyquist** (Carnegie Mellon, 1990s), a Lisp in which sounds are values, became
Audacity's plugin language.

PHI: Nyquist, as in Audacity: the effects of Process > Effect and its Generate submenu (Pluck,
Rhythm Track, Risset Drum), and Tools > Nyquist Prompt, where anyone can type a process or a
sound of their own in Lisp or SAL.

- [MUSIC-N](https://en.wikipedia.org/wiki/MUSIC-N), Wikipedia.
- [Csound](https://en.wikipedia.org/wiki/Csound), Wikipedia.
- [Max (software)](https://en.wikipedia.org/wiki/Max_(software)), Wikipedia.
- [Pure Data](https://en.wikipedia.org/wiki/Pure_Data), Wikipedia.
- [SuperCollider](https://en.wikipedia.org/wiki/SuperCollider), Wikipedia.
- [Faust (programming language)](https://en.wikipedia.org/wiki/Faust_(programming_language)), Wikipedia.
- [Nyquist (programming language)](https://en.wikipedia.org/wiki/Nyquist_(programming_language)), Wikipedia.
- [List of audio programming languages](https://en.wikipedia.org/wiki/List_of_audio_programming_languages), Wikipedia.

## Sonification

Data turned into sound: numbers, measurements or pictures mapped to pitch, loudness, timbre and
time, so they can be heard rather than seen. The Geiger counter is the oldest example; auditory
display became a field in the 1990s, with Gregory Kramer and the International Community for
Auditory Display, and NASA sonifies telescope images today.

PHI: **Sonify** (Visualizer > Sonify) plays pictures, still or moving, as notes: higher is a
higher note, brighter is louder, change strikes drums.

- [Sonification](https://en.wikipedia.org/wiki/Sonification), Wikipedia.

## Neural synthesis

Synthesis learned from examples by a neural network. **WaveNet** (DeepMind, 2016) generated
audio a sample at a time; Google Magenta's NSynth (2017) blended the timbres of instruments in a
learned space; **DDSP** (2020) lets a network drive classical oscillators and filters, so a voice
can become a violin; IRCAM's **RAVE** (2021), a variational autoencoder, runs in real time as a
plugin. Since 2023 text-to-music models generate whole pieces from a description. It is the
newest kind, and its questions of training data and authorship are still open.

- [WaveNet](https://en.wikipedia.org/wiki/WaveNet), Wikipedia.
- [Variational autoencoder](https://en.wikipedia.org/wiki/Variational_autoencoder), Wikipedia.
- [Generative artificial intelligence](https://en.wikipedia.org/wiki/Generative_artificial_intelligence), Wikipedia.
- Jesse Engel et al., "DDSP: Differentiable Digital Signal Processing", ICLR 2020
  ([arXiv:2001.04643](https://arxiv.org/abs/2001.04643)).
- Antoine Caillon and Philippe Esling, "RAVE: A variational autoencoder for fast and
  high-quality neural audio synthesis" ([arXiv:2111.05011](https://arxiv.org/abs/2111.05011)).
