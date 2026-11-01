<!--
Research > Engineering > Plugin Analyzer: the terms of Analyze > Plugin Analyzer (Ctrl+Alt+U,
also Master > Plugin Analyzer and A on a Master Section slot) and its tabs.
-->

# Engineering / Plugin Analyzer

## The Plugin Analyzer

A measuring bench for effects: it plays known test signals through a plugin and reads what comes
out, to show what the plugin does to sound beyond what its knobs say. It measures a copy of the
plugin with the same settings, so nothing heard changes, and with **Live** on it measures again
whenever a setting changes, so you can turn a knob and watch the curve move.

It can measure one slot of the Master Section, the whole Master Section, or a mixer track's
inserts; **Load a Plugin** puts one into a free slot, bypassed, to try it. **Level** sets the peak
level of the test signals (distortion and compression depend on it), **Input** feeds both
channels or only one (the other silent, which shows the crosstalk), and the ticks choose which
tests run: Frequency, Harmonics, Aliasing, Dynamics and Modulation. Each has its tab, with the
Impulse tab and Forensic Φ beside them, and **Copy Report** puts every result on the clipboard as
text.

The analyser follows DDMF's Plugin Doctor and the measuring practice of audio test sets such as
Audio Precision's: the same measurements a laboratory makes of an amplifier, made of software.

- [Audio system measurements](https://en.wikipedia.org/wiki/Audio_system_measurements),
  Wikipedia.
- Bob Metzler, *Audio Measurement Handbook* (Audio Precision, 1993).
- Joseph D'Appolito, *Testing Loudspeakers* (Audio Amateur Press, 1998).

## Linear, nonlinear and time-invariant

The two properties that decide how an effect can be measured. An effect is **linear** when
playing two sounds through it together gives the sum of what each gives alone, and twice as loud
in gives twice as loud out: equalisers, filters, delays and reverbs are linear. It is
**time-invariant** when it does the same thing now as a second later: a static equaliser is, a
phaser or a tremolo is not.

A linear, time-invariant (LTI) effect is described completely by its
[impulse response](research:Impulse%20response), and so by its frequency, phase and delay
responses: the Frequency and Impulse tabs. What a linear measurement cannot see needs tests of
its own: added harmonics and [aliasing](research:Aliasing) are the signs of a **nonlinear**
effect (saturation, compression, limiting), and level or pitch that moves on its own is the sign
of a **time-variant** one (modulation, gates). The analyser notes when an effect is either.

- [Linear time-invariant system](https://en.wikipedia.org/wiki/Linear_time-invariant_system),
  Wikipedia.
- Alan V. Oppenheim and Alan S. Willsky, *Signals and Systems* (Prentice Hall, 2nd ed. 1996).

## Exponential sine sweep

The test signal of the Frequency tab: a sine whose frequency glides from 10 Hz to just below the
Nyquist frequency in two seconds, rising by the same number of octaves each second. Played
through the effect and then **deconvolved**, the recording divided by the sweep in the frequency
domain, it gives the effect's impulse response. Because the sweep spends equal time in each
octave it has the spectrum of pink noise and a good signal-to-noise ratio in the bass.

Its great property is that the harmonics an effect adds come out of the deconvolution ahead of
the impulse in time, each as an impulse response of its own, so one sweep measures the linear
response and the harmonic distortion at once: PHI reads H2 to H5 over frequency from them. PHI
regularises the division (after Müller and Massarani) so that frequencies outside the sweep do
not blow up into noise.

Angelo Farina of the University of Parma published the method in 2000; it replaced maximum-length
sequences (MLS) as the usual way to measure rooms and equipment, and room-measurement programs
such as REW use it.

- [Impulse response](https://en.wikipedia.org/wiki/Impulse_response), Wikipedia.
- Angelo Farina, "Simultaneous Measurement of Impulse Response and Distortion with a
  Swept-Sine Technique", AES 108th Convention (Paris, 2000).
- Swen Müller and Paulo Massarani, "Transfer-Function Measurement with Sweeps", *JAES* 49
  (2001).

## Impulse response

What an effect gives back for a single click of one sample: for a linear, time-invariant effect,
a complete description of it, since every sound is a series of such clicks at different levels.
A plain gain gives back one click; an equaliser a short ringing; an echo a series of clicks; a
reverb a long decaying wash. The **Impulse** tab draws it over the first 10, 50 or 200 ms, or
all of it, measured from the sweep.

Convolution reverbs work the other way round: they play an impulse response measured in a real
room back through every sound.

- [Impulse response](https://en.wikipedia.org/wiki/Impulse_response), Wikipedia.
- [Convolution reverb](https://en.wikipedia.org/wiki/Convolution_reverb), Wikipedia.

## Frequency response

How much an effect turns each frequency up or down, in dB, from 10 Hz to the Nyquist frequency:
the **magnitude** curve of the Frequency tab, the Fourier transform of the impulse response. A
transparent effect is a flat line at 0 dB; an equaliser draws its bands; a filter its slope. The
curve shows what the knobs promise and what they do: the actual corner frequency, the width of a
bell, the ripple of a linear-phase filter, the cramping of a digital bell near Nyquist.

- [Frequency response](https://en.wikipedia.org/wiki/Frequency_response), Wikipedia.

## Phase response

How far each frequency is shifted in its cycle by the effect, in degrees, from −180° to +180° (the
curve wraps round at the ends). Every ordinary analogue-style filter shifts the phase around the
frequencies it changes; a **linear-phase** filter delays every frequency by the same time, so
its phase falls in a straight line, and a pure delay does the same. Phase shift is mostly
inaudible on its own but matters when the processed sound is mixed with an unprocessed copy, as
in parallel processing or multi-microphone recordings, where frequencies can cancel.

The Frequency tab shows the phase under the magnitude, or the group delay in its place.

- [Phase response](https://en.wikipedia.org/wiki/Phase_response), Wikipedia.
- [Linear phase](https://en.wikipedia.org/wiki/Linear_phase), Wikipedia.

## Group delay

How late each frequency comes out, in milliseconds: the rate at which the phase changes with
frequency. A pure delay or a linear-phase filter delays every frequency alike, so the group
delay is a flat line; an ordinary equaliser delays the frequencies around its bands more than
the rest; a steep low-cut filter can delay the deep bass by tens of milliseconds, which is heard
as a looser low end. It is easier to read than phase because it does not wrap.

- [Group delay and phase delay](https://en.wikipedia.org/wiki/Group_delay_and_phase_delay),
  Wikipedia.

## Latency

The delay an effect adds between its input and its output, in samples. Plugins that look ahead
(limiters), work in blocks (FFT processes) or oversample report their latency to the host, which
delays everything else to match (**latency compensation**). The Plugin Analyzer shows the
latency the plugin reports and the one it measures, from where the impulse response peaks; when
the two disagree, the plugin will sit out of time with the other tracks.

- [Latency (audio)](https://en.wikipedia.org/wiki/Latency_(audio)), Wikipedia.

## THD and THD+N

**Total harmonic distortion** (THD) is how much a steady tone gains in harmonics on its way through
an effect: the power of all the harmonics together (twice, three times... the frequency) against
the power of the tone, as a percentage. **THD+N** (THD plus noise) counts everything in the output
that is not the tone, noise and inharmonic products included. A clean digital equaliser shows
next to nothing; a tape or tube emulation a few percent; a fuzz far more.

The **Harmonics** tab plays a 1 kHz sine (placed exactly on an FFT bin, so that every harmonic
lands on one too) and shows the output's spectrum, the THD and THD+N of each channel, the gain
the tone received, the level of each harmonic up to the tenth, and any tones that are no harmonic
at all (from a pitch shifter, a ring modulator, or aliasing).

The measures come from amplifier engineering; THD was the headline figure of the hi-fi era, which
taught engineers that its single number says little about how distortion sounds.

- [Total harmonic distortion](https://en.wikipedia.org/wiki/Total_harmonic_distortion),
  Wikipedia.
- Earl R. Geddes and Lidia W. Lee, "Auditory Perception of Nonlinear Distortion", AES 115th
  Convention (New York, 2003).

## Harmonics H2 to H5

The harmonics an effect adds, one by one: H2 at twice the frequency of the tone, H3 at three times,
and so on. Their mix is the colour of distortion. **Even** harmonics (H2, H4) are octaves and
fifths of the tone and sound warm and full, the character of single-ended tube stages and tape;
**odd** harmonics (H3, H5) sound harder and more forward, the character of symmetrical clipping,
transistor stages and transformers. How they grow with level and frequency tells a gentle
saturator from a harsh one.

The Harmonics tab shows H2 to H5 over frequency, read from the [sweep](research:Exponential%20sine%20sweep),
below the spectrum of the single tone.

- [Harmonic](https://en.wikipedia.org/wiki/Harmonic), Wikipedia.
- [Distortion (music)](https://en.wikipedia.org/wiki/Distortion_(music)), Wikipedia.

## Aliasing

What a nonlinear effect produces when the harmonics it creates go above the
[Nyquist frequency](research:Sample%20rate%20and%20the%20Nyquist%20frequency): they cannot exist
at that sample rate, so they fold back below it, mirrored around the Nyquist frequency, as tones
that are no harmonic of anything played. A 7.5 kHz tone through a clipper at 48 kHz makes a fifth
harmonic at 37.5 kHz, which comes out at 10.5 kHz. Aliasing sounds harsh, grainy and out of tune,
and grows with the frequency of the input.

The **Aliasing** tab plays steady tones from 1 kHz to 20 kHz, separates the true harmonics from the
folded ones in each output, and plots the aliasing of each tone against its frequency, with the
spectrum of the 7.5 kHz tone, harmonics and folded components marked. Effects avoid it by
**oversampling**: running at a higher rate inside, so harmonics have room above, then filtering
before coming back down.

- [Aliasing](https://en.wikipedia.org/wiki/Aliasing), Wikipedia.
- [Oversampling](https://en.wikipedia.org/wiki/Oversampling), Wikipedia.
- Julian D. Parker, Vadim Zavalishin and Efflam Le Bivic, "Reducing the Aliasing of Nonlinear
  Waveshaping Using Continuous-Time Convolution", DAFx-16 (Brno, 2016).

## Transfer curve, threshold and ratio

How a dynamics processor's output level depends on its input level. The **Dynamics** tab plays a
tone at every level from −60 to 0 dBFS in 2 dB steps, lets it settle, and draws level out against
level in: a straight diagonal for an effect that leaves dynamics alone, a curve that bends over
above the **threshold** for a compressor or limiter, and one that drops away below it for a gate or
expander. The **ratio** is how many dB more in it takes to give one dB more out above the threshold:
4:1 for a firm compressor, 10:1 or more for a limiter. PHI reads the steepest stretch of the curve
as the ratio and where it bends as the threshold.

See also [Compression](research:Compression).

- [Dynamic range compression](https://en.wikipedia.org/wiki/Dynamic_range_compression),
  Wikipedia.
- Dimitrios Giannoulis, Michael Massberg and Joshua D. Reiss, "Digital Dynamic Range Compressor
  Design: A Tutorial and Analysis", *JAES* 60 (2012).

## Attack and release

How fast a dynamics processor reacts. **Attack** is how long it takes to turn the sound down once
it rises above the threshold, **release** how long to let it back up once it falls. Fast attack
catches every transient but dulls the punch; slow attack lets the hit through and then holds the
sustain; release sets whether the compression pumps with the music or holds steady.

The Dynamics tab plays a step in level, from −40 to −6 dBFS and back, and draws the gain the
effect gives over time; attack and release are read as the time to 63 % of the full change, the
time constant of an exponential, which is how most compressors define their dials.

- [Dynamic range compression](https://en.wikipedia.org/wiki/Dynamic_range_compression),
  Wikipedia, "Attack and release".

## Modulation: tremolo, vibrato and gates

Effects that change the sound over time on their own: **tremolo** moves the level up and down,
**vibrato** the pitch, both driven by a low-frequency oscillator (**LFO**) a few times a second;
choruses, flangers and phasers move a delay or a filter the same way; a rhythmic **gate** switches
the sound on and off in a pattern. The **Modulation** tab plays a long steady tone for twelve
seconds and follows its level and pitch at the output: the depth of the level swing in dB, the
depth of the pitch swing in cents, the LFO rates found (strongest first), and, for a gate, the
shortest open or closed step and the rhythm counted in those steps.

- [Low-frequency oscillation](https://en.wikipedia.org/wiki/Low-frequency_oscillation),
  Wikipedia.
- [Tremolo](https://en.wikipedia.org/wiki/Tremolo), Wikipedia.
- [Vibrato](https://en.wikipedia.org/wiki/Vibrato), Wikipedia.

## Crosstalk

Sound from one channel leaking into the other. With **Input** set to Left only or Right only,
the Plugin Analyzer feeds the test signals to one channel and keeps the other silent, so whatever
comes out of the silent side is crosstalk, deliberate (a stereo widener, a ping-pong delay, a
reverb) or not (a mono-summing processor, an emulation of analogue channel bleed).

- [Crosstalk](https://en.wikipedia.org/wiki/Crosstalk), Wikipedia.

## Forensic Φ

PHI's own test: whether a plugin's designer seems to have chosen the golden ratio. It collects the
numbers a designer chooses, found in the other measurements (the frequencies of peaks, notches
and phase turns, echo times and how much quieter each echo is, the tones a pitch shifter adds, a
compressor's ratio and its attack against its release, LFO rates, a gate's rhythm, the latency),
and looks for golden ratios between them (√φ, φ, φ^1.5, φ² and so on to φ⁶) and for golden
numbers and Fibonacci numbers among them.

Measure enough numbers and some will fall near φ by chance, so every match is weighed against
chance: the tolerance of each comparison comes from how exactly its numbers are measured; the
chance of finding as many hits as were found is worked out (a Poisson tail) for each kind of
evidence, at the full tolerance and at a fifth of it; the most telling kind decides, its chance
multiplied by how many were tried; and the same search is run for a thousand other ratios from 1.40
to 1.90, the **control scan**, so that φ only stands out if it beats nearly all of them. Ratios that
are also simple fractions are left out. The verdict is nothing, chance, suggestive or strong.

The method is that of any significance test with a correction for multiple comparisons, the
statisticians' guard against finding patterns in noise.

- [Multiple comparisons problem](https://en.wikipedia.org/wiki/Multiple_comparisons_problem),
  Wikipedia.
- [P-value](https://en.wikipedia.org/wiki/P-value), Wikipedia.
- See also [The golden ratio](research:The%20golden%20ratio) and
  [The golden ratio in music](research:The%20golden%20ratio%20in%20music).
