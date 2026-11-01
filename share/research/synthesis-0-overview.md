<!--
Research > Synthesis (owner, 2026-10-31): "the many, major types of synthesis with examples,
history from origins to latest virtual synths. Wikipedia as usual." Three files, numbered for
the order of the submenus: 0 what synthesis is and which of PHI's instruments use which kind,
1 the types, each with what it is, the instruments that made it known and where PHI uses it,
2 the history, from the first electrical instruments to today's software synths.
-->

# Synthesis

## Sound synthesis

Making sound from nothing but a description of it: an electrical or numerical process
generates the waveform, rather than a microphone capturing one that already exists. A
synthesizer is an instrument built to do this under a player's control, usually from a
keyboard, today often from MIDI.

Every synthesizer combines a few kinds of part. **Sources** make the raw sound: oscillators
(a sine, a sawtooth, a square, a stored wave), noise, or a recording. **Shapers** change its
colour: filters, waveshapers, modulation of one source by another. **Controls** change all of
it over time: envelopes for each note, low-frequency oscillators for vibrato and sweeps, the
player's velocity, pitch bend and pedals. A synthesis method is a particular choice of source
and shaper, and each gives sounds the others find hard: subtractive the warm, filtered analogue
tone, FM the bell and the electric piano, sampling the real instrument, granular the cloud,
physical modelling the plucked string that responds to how it is played.

The word came into music with the RCA Mark II Sound Synthesizer (1957), and stuck with Robert
Moog's modular instruments in the 1960s. The [history](research:From%20the%20telegraph%20to%20the%20Telharmonium)
starts eighty years before that.

- [Synthesizer](https://en.wikipedia.org/wiki/Synthesizer), Wikipedia.
- [Electronic musical instrument](https://en.wikipedia.org/wiki/Electronic_musical_instrument), Wikipedia.
- [List of synthesizers](https://en.wikipedia.org/wiki/List_of_synthesizers), Wikipedia.
- Curtis Roads, *The Computer Music Tutorial* (MIT Press, 1996; 2nd ed. 2023).
- Martin Russ, *Sound Synthesis and Sampling* (Focal Press, 3rd ed. 2009).

## Synthesis in PHI

PHI's instruments cover most of the major methods, each named by what it does:

| PHI's instrument | Where | Method | Original name |
| --- | --- | --- | --- |
| Synth | built in, the mixer's and patchbay's default | [Subtractive](research:Subtractive%20synthesis) (four waves, low-pass, envelope) and noise drums | PHI's own |
| Subtractive1 mono | Synth menu | [Subtractive](research:Subtractive%20synthesis), after the Minimoog Model D | CA-72 |
| Subtractive2 poly | Synth menu | [Subtractive](research:Subtractive%20synthesis) | Helm |
| FM | Synth menu | [FM](research:FM%20synthesis), four operators | OctaSine |
| Additive | Synth menu | [Additive](research:Additive%20synthesis) | Overtonium |
| Wavetable | Synth menu | [Wavetable](research:Wavetable%20synthesis) | Vaporizer2 |
| Modular | Synth menu | [Modular](research:Modular%20synthesis), with VCV Fundamental's modules | Cardinal |
| Simplest, Slicer | Sample menu | [Sampling](research:Sample-based%20synthesis) | PHI's own |
| Retro Sampler | Sample menu | [Sampling](research:Sample-based%20synthesis), in the manner of the Amiga | Ami |
| Granular | Sample menu | [Granular](research:Granular%20synthesis) | GRNLR |
| PHI rāga | the mixer's instruments | [Physical modelling](research:Physical%20modelling%20synthesis): a waveguide sitar | PHI's own |
| The splash voice | PHI's start | [Phase distortion](research:Phase%20distortion%20synthesis) through a granular reverb | PHI's own |
| Tracker1 | Tracker menu | [Sampling](research:Sample-based%20synthesis): the module formats | MilkyTracker |
| Tracker2 | Tracker menu | [Sound chips](research:Sound%20chips%20and%20chiptune): FM, PSG and wavetable chips | Furnace |
| Nyquist | Process > Effect (Generate: Pluck, Risset Drum), Tools > Nyquist Prompt | [Synthesis by program](research:Synthesis%20languages) | Nyquist |
| Generate Tone, Generate Noise | Generate menu | The [oscillator](research:Oscillators%20and%20waveforms) waves and coloured noise | PHI's own |
| Sonify | Visualizer > Sonify | [Sonification](research:Sonification): pictures played as notes | PHI's own |
| Extreme Stretch | Warp, the mixer's clips | [Spectral](research:Spectral%20synthesis): the Paulstretch algorithm | PHI's own |

The Φ synth, PHI's own phase distortion synth, is still to come; the voice heard at the splash
is its stand-in.

- [Software synthesizer](https://en.wikipedia.org/wiki/Software_synthesizer), Wikipedia.
- [Virtual Studio Technology](https://en.wikipedia.org/wiki/Virtual_Studio_Technology), Wikipedia.
- [LV2](https://en.wikipedia.org/wiki/LV2), Wikipedia.

## Oscillators and waveforms

An oscillator repeats a wave at a frequency: the pitch. Four shapes have been the starting point
of synthesis since the 1960s, because analogue circuits make them easily and each has a
distinct set of harmonics. The **sine** has only its fundamental: pure, flute-like. The
**triangle** has the odd harmonics, falling fast (1/n²): soft, hollow. The **square** has the
odd harmonics at 1/n: hollow and reedy, the clarinet; a **pulse** narrower than half the cycle
adds the even ones and grows thinner and nasal. The **sawtooth** has every harmonic at 1/n:
bright and buzzy, the strings and brass of analogue synths. **Noise** has no pitch at all:
white noise has equal energy at every frequency, pink equal energy in every octave, and they
make wind, breath, cymbals and snares.

In digital synthesis the sharp corners of a square or a sawtooth hold harmonics above the
Nyquist frequency, which fold back as aliasing, tones that were never there; band-limited oscillators
(BLIT, BLEP, wavetables made per octave) leave them out.

PHI: Generate Tone makes all four, band-limited by default, with a sweep; Generate Noise makes
white, pink, brown, blue and violet. The built-in Synth plays the same four waves.

- [Electronic oscillator](https://en.wikipedia.org/wiki/Electronic_oscillator), Wikipedia.
- [Voltage-controlled oscillator](https://en.wikipedia.org/wiki/Voltage-controlled_oscillator), Wikipedia.
- [Sawtooth wave](https://en.wikipedia.org/wiki/Sawtooth_wave), Wikipedia.
- [Square wave](https://en.wikipedia.org/wiki/Square_wave), Wikipedia.
- [Triangle wave](https://en.wikipedia.org/wiki/Triangle_wave), Wikipedia.
- [Pulse wave](https://en.wikipedia.org/wiki/Pulse_wave), Wikipedia.
- [Colors of noise](https://en.wikipedia.org/wiki/Colors_of_noise), Wikipedia.
- [Aliasing](https://en.wikipedia.org/wiki/Aliasing), Wikipedia.

## Envelopes and modulation

What keeps a synthesized sound from being a test tone is change. An **envelope** shapes a sound
over each note; the usual one is **ADSR**: Attack (the rise when the key goes down), Decay (the
fall to), Sustain (the level held while the key is down), Release (the fade after it comes up).
It was proposed by Vladimir Ussachevsky and first built by Robert Moog in the 1960s. A
**low-frequency oscillator** (LFO), below about 20 Hz, adds vibrato to pitch, tremolo to level
and wah to a filter. **Modulation routing** decides what moves what: a fixed path on the
Minimoog, a patch cord on a modular, a matrix on the Oberheim Matrix-12 or in most software
synths, where any source can reach any destination.

PHI: the built-in Synth's Attack, Decay, Sustain and Release; Simplest's envelope and fades;
the Device Chain's modulator rack (LFO, Attack-Decay, Step, Random, Sample & Hold, Envelope
Follower and PHI's own Φ Flow), which moves any parameter of the plugins in the chain.

- [Envelope (music)](https://en.wikipedia.org/wiki/Envelope_(music)), Wikipedia.
- [Low-frequency oscillation](https://en.wikipedia.org/wiki/Low-frequency_oscillation), Wikipedia.
- [Vibrato](https://en.wikipedia.org/wiki/Vibrato), Wikipedia.
- [CV/gate](https://en.wikipedia.org/wiki/CV/gate), Wikipedia.
