<!--
Research > Engineering > Process: the terms of the Process menu and its Effect submenu (Repeat
Last Effect, Generate, and the Nyquist effects that ship with PHI).
-->

# Engineering / Process

## Destructive and non-destructive editing

The Process menu changes the samples of the file itself, over the selection or, without one, the
whole file: **destructive** editing, as in Sound Forge and WaveLab's audio editor. The Master
Section, by contrast, is **non-destructive**: its plugins change what is heard and leave the file
alone until Render in Place writes them in. PHI keeps every destructive step in the undo history,
so nothing is final until the file is saved.

Destructive editing is the older way, from cutting and splicing tape; non-destructive editing came
with digital workstations that could play a chain of processes in real time.

- [Non-linear editing](https://en.wikipedia.org/wiki/Non-linear_editing), Wikipedia.
- [Digital audio workstation](https://en.wikipedia.org/wiki/Digital_audio_workstation),
  Wikipedia.

## Gain

Multiplying every sample by the same number, set in decibels: +6 dB doubles the amplitude, −6 dB
halves it. Process > Gain sets any amount; Gain Up and Gain Down move it by 0.5 dB at a time. Gain
changes level and nothing else, but turning up can push peaks past full scale, where a fixed-point
file will clip once it is saved.

Setting the level of each stage so that the signal sits well above the noise and below clipping is
**gain staging**, the first discipline of recording and mixing.

- [Gain (electronics)](https://en.wikipedia.org/wiki/Gain_(electronics)), Wikipedia.
- [Headroom (audio signal processing)](https://en.wikipedia.org/wiki/Headroom_(audio_signal_processing)),
  Wikipedia.

## Normalization

Gain worked out for you, to reach a target. Process > Normalize offers two kinds:

- **Peak level (dBFS)**: turns the audio up or down until its highest sample reaches the target.
  It says nothing about how loud the result sounds; two files normalised to −1 dBFS can differ in
  loudness by 10 LU or more.
- **Integrated loudness (LUFS)**: turns it until its [integrated loudness](research:Momentary,%20short-term%20and%20integrated%20loudness)
  reaches the target, with the same presets as the [loudness targets](research:Loudness%20targets),
  or the loudness of another file (**Match a File**). Because turning up can push the
  [true peak](research:True%20peak%20and%20dBTP) past full scale, **Limit the true peak** limits the
  peaks to a ceiling and then turns up a touch more, so the loudness still lands on target.

Streaming services do loudness normalisation on playback, turning loud masters down to their
reference; that is what made the "loudness war" of ever-louder masters pointless.

- [Audio normalization](https://en.wikipedia.org/wiki/Audio_normalization), Wikipedia.
- [Loudness war](https://en.wikipedia.org/wiki/Loudness_war), Wikipedia.

## Fades and fade shapes

A fade raises the level from silence (fade in) or lowers it to silence (fade out) over the
selection. Its **shape** is the curve the gain follows, chosen with Process > Fade Shape:

- **Linear**: the gain rises in a straight line. Simple, but it sounds as if it stays loud and
  then drops away at the end, because hearing is logarithmic.
- **Equal Power**: the gain follows a quarter of a sine, so a fade out and a fade in laid over
  each other keep the total power constant: the right curve for a crossfade between two different
  sounds.
- **Exponential**: the gain rises as its square, slowly at first and late at the end, which sounds
  smoother on a fade out of music.
- **S-Curve**: half a cosine, gentle at both ends and quicker in the middle, for fades that should
  begin and end imperceptibly.

The Effect submenu's Nyquist effects add Adjustable Fade and Studio Fade Out (see
[Fades in the Effect submenu](research:Fades%20in%20the%20Effect%20submenu)).

- [Fade (audio engineering)](https://en.wikipedia.org/wiki/Fade_(audio_engineering)),
  Wikipedia.
- [Crossfade](https://en.wikipedia.org/wiki/Crossfade), Wikipedia.

## Reverse

Plays the selection backwards: the last sample first. A struck note becomes a swell that stops
dead; a reverb tail becomes a rising whoosh before the sound ("reverse reverb", made by reversing,
adding reverb and reversing again).

Backwards tape became a studio effect with Pierre Schaeffer's musique concrète in the late 1940s
and reached pop with the Beatles' "I'm Only Sleeping" and "Rain" (1966).

- [Backmasking](https://en.wikipedia.org/wiki/Backmasking), Wikipedia.
- [Musique concrète](https://en.wikipedia.org/wiki/Musique_concr%C3%A8te), Wikipedia.

## Polarity inversion

Process > Invert Polarity turns the waveform upside down: every sample is multiplied by −1. On its
own it sounds the same; against another signal it decides whether the two add up or cancel. It is
used to fix a microphone or cable wired the wrong way round, to line up the top and bottom
microphones of a snare drum, and to test a null (an inverted copy added to the original leaves
only what differs).

It is often called "phase invert", but polarity is not phase: inverting flips every frequency at
once, while a phase shift delays frequencies by differing parts of a cycle.

- [Electrical polarity](https://en.wikipedia.org/wiki/Electrical_polarity), Wikipedia.
- [Phase (waves)](https://en.wikipedia.org/wiki/Phase_(waves)), Wikipedia.

## DC offset

A constant added to the whole waveform, so that it no longer centres on zero: a fault of
converters and analogue stages, and the result of some processes. It is inaudible in itself but
wastes headroom (the waveform reaches full scale sooner on one side), makes cuts and fades click
because silence is no longer at zero, and biases compressors. Process > Remove DC Offset
measures the mean of each channel over the selection and subtracts it; Amplitude Statistics shows
it as a percentage of full scale.

- [DC bias](https://en.wikipedia.org/wiki/DC_bias), Wikipedia.

## Stereoize

Making stereo from mono (Process > Stereoize). PHI adds a delayed copy of the upper part of the
sound to the left channel and subtracts the same from the right: the two channels become
**complementary comb filters**, each loud where the other is quiet, so the sound spreads across
the speakers, yet left plus right is exactly the original, so the result folds back to mono
without the hollow colour of comb filtering. **Width** is the level of what is added, **Delay**
(1 to 40 ms) shorter for brighter and tighter or longer for roomier, and **Keep bass centred
below** leaves the low end in the middle. With **Golden ratio** on, four allpass filters spaced by
1/φ take the place of the single delay, so the comb's teeth are no longer evenly spaced and the
width sounds less pitched.

Pseudo-stereo by complementary combs was devised for broadcasting in the 1950s, from the work of
Holger Lauridsen at Danish State Radio, and was how many mono records were reissued as
"electronically re-channelled stereo" in the 1960s.

- [Stereophonic sound](https://en.wikipedia.org/wiki/Stereophonic_sound), Wikipedia.
- [Comb filter](https://en.wikipedia.org/wiki/Comb_filter), Wikipedia.
- [All-pass filter](https://en.wikipedia.org/wiki/All-pass_filter), Wikipedia.

## Mid/side width

Widening or narrowing a stereo image by working on its [mid and side](research:Mono,%20stereo,%20mid%20and%20side)
separately (Process or Spatial > Mid/Side Width, also a live effect in the Master Section):

- **Width**: the side's level, from 0 (mono) to 200 %, or 300 % with Huge, which pulls back when
  the [correlation](research:Phase%20correlation) falls below −0.3.
- **Mono below**: the side is removed below a crossover (20 to 500 Hz), a Linkwitz-Riley filter of
  two Butterworths in a row, for a tight, centred bass that cuts well on vinyl and in clubs.
- **Generate**: made-up side from the mid above 150 Hz, as Stereoize does.
- **Air**: a high shelf at 8 kHz on the side only, to open the top of the image.
- **Rotation**: turns the whole image left or right by up to 45°.
- **Lift**: up to twice the width on louder moments.
- **Match level**: keeps the output as loud as the input, so the comparison is fair.
- **Listen**: stereo, mono, the side alone, or the bass alone, to check what each part does.

M/S processing has been part of mastering since the stereo cutting lathes of the 1960s, when the
side of the bass had to be reduced to keep the groove playable.

- [Stereo imaging](https://en.wikipedia.org/wiki/Stereo_imaging), Wikipedia.
- [Linkwitz–Riley filter](https://en.wikipedia.org/wiki/Linkwitz%E2%80%93Riley_filter),
  Wikipedia.
- [Butterworth filter](https://en.wikipedia.org/wiki/Butterworth_filter), Wikipedia.

## Sample-rate conversion

Changing the sample rate of a file (Process > Convert Sample Type), for instance from 96 kHz to
44.1 kHz for a CD. The new samples fall between the old ones and have to be computed from them,
and going down the frequencies above the new Nyquist frequency must be filtered out first, or they
will [alias](research:Aliasing). PHI uses a windowed-sinc filter with a Kaiser window: flat to 95 %
of the lower Nyquist frequency and more than 90 dB of rejection above it.

The sinc function is the ideal interpolator of the sampling theorem; windowing it makes it finite.
Julius Smith and Phil Gossett's bandlimited interpolation (1984) is the method most audio
resamplers follow.

- [Sample-rate conversion](https://en.wikipedia.org/wiki/Sample-rate_conversion), Wikipedia.
- [Sinc filter](https://en.wikipedia.org/wiki/Sinc_filter), Wikipedia.
- [Kaiser window](https://en.wikipedia.org/wiki/Kaiser_window), Wikipedia.
- Julius O. Smith and Phil Gossett, "A Flexible Sampling-Rate Conversion Method", ICASSP (1984).

## Bit-depth reduction, TPDF dither and noise shaping

Convert Sample Type and Export can lower the bit depth to 24, 16 or 8 bits, with a choice of
**Dither**: **None** rounds each sample to the nearest step, and the rounding error follows the
signal as distortion; **TPDF** adds, before rounding, a little noise of triangular probability
(the sum of two random values), which makes the error independent of the signal: plain, steady
hiss instead of distortion, and quiet details fade into it rather than breaking up; **TPDF with
noise shaping** feeds the rounding error back through a filter that moves the hiss up towards the
highest frequencies, where hearing is least sensitive, so it sounds quieter at the same depth.

Dither belongs once, at the very end, when the file is made for delivery.

See also [Dither](research:Dither) and [Bit depth and quantization](research:Bit%20depth%20and%20quantization).

- [Dither](https://en.wikipedia.org/wiki/Dither), Wikipedia.
- [Noise shaping](https://en.wikipedia.org/wiki/Noise_shaping), Wikipedia.
- Stanley P. Lipshitz, Robert A. Wannamaker and John Vanderkooy, "Quantization and Dither: A
  Theoretical Survey", *JAES* 40 (1992).

## Channel conversion

Convert Sample Type can also make a file mono, every channel averaged into one, or stereo, a mono
file copied to two equal channels. Mixing to mono is where [phase correlation](research:Phase%20correlation)
matters: anything opposite in the two channels cancels. Files of more channels are played and
measured as a stereo fold-down with the ITU-R BS.775 gains (centre and surrounds at −3 dB, the LFE
left out).

- [Surround sound](https://en.wikipedia.org/wiki/Surround_sound), Wikipedia.
- [Monaural](https://en.wikipedia.org/wiki/Monaural), Wikipedia.

# Engineering / Process / Effect

## The Effect submenu

Process > Effect holds the processes that are programs rather than built-in commands: **Repeat
Last Effect** (Ctrl+Shift+Y) runs the last one again with the same settings; **Generate** makes
new sound in place of the selection, PHI's own Tone and Noise first, then the
[Nyquist](research:Nyquist) generators; below them come the Nyquist effects. Nyquist plug-ins of
the analyze type go to the Analyze menu and tools to the Tools menu. The ones PHI ships are
Audacity's, so they behave as Audacity's users know them; any other .ny file put in the plug-ins
folder joins them.

- [Audio plug-in](https://en.wikipedia.org/wiki/Audio_plug-in), Wikipedia.
- [Nyquist (programming language)](https://en.wikipedia.org/wiki/Nyquist_(programming_language)),
  Wikipedia.

## Test tones and sweeps

Generate > Tone makes one of the four classic oscillator waves at a set peak level:

- **Sine**: a single frequency, no harmonics: the reference of measurement and calibration.
- **Square**: odd harmonics falling as 1/n, hollow and reedy; its **pulse width**, the part of
  each cycle that is high, changes its harmonics (at 50 % the even ones are missing).
- **Sawtooth**: every harmonic falling as 1/n, bright and buzzy, the raw material of subtractive
  synthesis.
- **Triangle**: odd harmonics falling as 1/n², soft, between a sine and a square.

A different start and end frequency makes a **sweep**, gliding evenly in pitch, for testing
filters, speakers and rooms. **Band-limited** keeps every harmonic below the Nyquist frequency;
turned off, the raw wave's corners [alias](research:Aliasing) into inharmonic tones.

- [Sine wave](https://en.wikipedia.org/wiki/Sine_wave), Wikipedia.
- [Square wave](https://en.wikipedia.org/wiki/Square_wave), Wikipedia.
- [Sawtooth wave](https://en.wikipedia.org/wiki/Sawtooth_wave), Wikipedia.
- [Triangle wave](https://en.wikipedia.org/wiki/Triangle_wave), Wikipedia.
- [Chirp](https://en.wikipedia.org/wiki/Chirp), Wikipedia.

## Coloured noise

Generate > Noise makes random signals named, by analogy with light, after the slope of their
spectrum: **white** has equal energy per hertz and sounds like hiss; **pink** falls 3 dB per octave,
equal energy per octave, the balance of music and the standard signal for tuning speakers and
rooms; **brown** (or red, after Brownian motion) falls 6 dB per octave and rumbles; **blue** rises
3 dB per octave and **violet** 6 dB, thin and sizzling. The same seed always gives the same noise.

- [Colors of noise](https://en.wikipedia.org/wiki/Colors_of_noise), Wikipedia.
- [White noise](https://en.wikipedia.org/wiki/White_noise), Wikipedia.
- [Pink noise](https://en.wikipedia.org/wiki/Pink_noise), Wikipedia.
- [Brownian noise](https://en.wikipedia.org/wiki/Brownian_noise), Wikipedia.

## Pluck

A Nyquist generator of a plucked string at a MIDI pitch, by the **Karplus-Strong** algorithm: a
short burst of noise circulates through a delay line one period long, averaged a little at each
pass, so the high frequencies die first, as on a real string. It was one of the first physical
models of an instrument.

Kevin Karplus and Alex Strong published it in 1983; David Jaffe and Julius Smith extended it into
the waveguide synthesis that followed.

- [Karplus–Strong string synthesis](https://en.wikipedia.org/wiki/Karplus%E2%80%93Strong_string_synthesis),
  Wikipedia.
- Kevin Karplus and Alex Strong, "Digital Synthesis of Plucked-String and Drum Timbres",
  *Computer Music Journal* 7 (1983).

## Risset Drum

A Nyquist generator of a drum sound after Jean-Claude Risset's: an inharmonic tone with a
decaying envelope, mixed with a band of noise around a centre frequency. Its controls are the
frequency, the decay, the noise band's centre and width, how much noise, and the amplitude.

Risset made it at Bell Labs for his *Introductory Catalogue of Computer Synthesized Sounds* (1969),
the first systematic book of digital sound recipes.

- [Jean-Claude Risset](https://en.wikipedia.org/wiki/Jean-Claude_Risset), Wikipedia.
- [Additive synthesis](https://en.wikipedia.org/wiki/Additive_synthesis), Wikipedia.

## Rhythm Track

A Nyquist generator of a click track: a beat at a tempo in beats per minute, a number of beats per
bar with the first accented, swing, a number of bars or a duration, a start offset, and the beat
sound (metronome tick, or tones at MIDI pitches for strong and weak beats). A click track keeps
musicians and edits in time.

- [Click track](https://en.wikipedia.org/wiki/Click_track), Wikipedia.
- [Metronome](https://en.wikipedia.org/wiki/Metronome), Wikipedia.
- [Swing (jazz performance style)](https://en.wikipedia.org/wiki/Swing_(jazz_performance_style)),
  Wikipedia.

## Fades in the Effect submenu

Two Nyquist fades beside the Process menu's own. **Adjustable Fade** fades up or down between any
two levels, as percentages or decibels, with a mid-fade adjustment that bends the curve and handy
presets (S-curves, partial fades). **Studio Fade Out** is a smooth fade out that also closes a
low-pass filter as it falls, so the sound dulls as it quietens, as faders on music do.

- [Fade (audio engineering)](https://en.wikipedia.org/wiki/Fade_(audio_engineering)),
  Wikipedia.

## Delay and echo

The Nyquist **Delay** repeats the sound a number of times, each repeat quieter by a set number of
dB: **Regular** at equal times, **Bouncing Ball** with the repeats coming ever faster, and
**Reverse Bouncing Ball** ever slower; each echo can also shift in pitch (with or without changing
its tempo).

Echo as an effect came from tape: the Echoplex (1961) and Roland Space Echo (1974) used loops of
tape passing a moving playback head.

- [Delay (audio effect)](https://en.wikipedia.org/wiki/Delay_(audio_effect)), Wikipedia.
- [Echo](https://en.wikipedia.org/wiki/Echo), Wikipedia.

## Limiter

The Nyquist **Legacy Limiter** keeps peaks below a level: **Hard Limit** cuts them off at it,
**Soft Limit** rounds them into it, and **Soft Clip** saturates gently. Input gain for each channel,
a hold time and make-up gain go with it. A limiter is a compressor with a very high ratio and fast
attack; the Master Section's true-peak limiter does the same job with look-ahead and oversampled
detection.

- [Limiter](https://en.wikipedia.org/wiki/Limiter), Wikipedia.
- [Clipping (audio)](https://en.wikipedia.org/wiki/Clipping_(audio)), Wikipedia.

## Noise gate

The Nyquist **Noise Gate** turns the sound down whenever it falls below a threshold, to silence
background noise, hum or bleed between notes and phrases. Its controls are the language of every
gate: **threshold**; **level reduction**, how far it turns down when closed (−100 dB is silence);
**attack**, how fast it opens; **hold**, how long it stays open after the sound falls; **decay**, how
fast it closes; **gate frequencies above**, to gate only the highs; and **stereo linking**, so both
channels open together. **Analyze Noise Level** measures the noise to choose the threshold.

- [Noise gate](https://en.wikipedia.org/wiki/Noise_gate), Wikipedia.

## Notch filter and Q

The Nyquist **Notch Filter** removes a narrow band around one frequency, for a whistle, a hum or a
resonance. Its **Q**, the quality factor, is the centre frequency divided by the width of the band:
a higher Q makes a narrower notch that takes less of the sound around it. Repair > DeHum uses a
series of such notches at a hum and its harmonics.

- [Band-stop filter](https://en.wikipedia.org/wiki/Band-stop_filter), Wikipedia.
- [Q factor](https://en.wikipedia.org/wiki/Q_factor), Wikipedia.

## Tremolo

The Nyquist **Tremolo** moves the level up and down with a low-frequency oscillator: its waveform
(sine, triangle, sawtooth, inverse sawtooth, square), rate in hertz, starting phase and wet level
(the depth). Guitar amplifiers built it in from the 1950s, Fender's among the first.

- [Tremolo](https://en.wikipedia.org/wiki/Tremolo), Wikipedia.
- [Low-frequency oscillation](https://en.wikipedia.org/wiki/Low-frequency_oscillation),
  Wikipedia.

## Spectral Delete and Spectral Edit Shelves

Two Nyquist effects that work on a selection of time and frequency drawn on the
[spectral display](research:Spectral%20display). **Spectral Delete** removes the selected band;
**Spectral Edit Shelves** raises or lowers it with low and high shelving filters, by up to 24 dB.
They are Audacity's spectral edits; PHI's own Spectral Gain, Auto Heal and the Repair tools do the
same kind of work on the short-time Fourier transform (see [Spectral editing](research:Spectral%20editing)).

- [Shelving filter](https://en.wikipedia.org/wiki/Equalization_(audio)#Shelving_filter),
  Wikipedia.
- [Equalization (audio)](https://en.wikipedia.org/wiki/Equalization_(audio)), Wikipedia.
