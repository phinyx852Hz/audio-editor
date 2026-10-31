<!--
Research > Engineering > Analysis Meters: the terms of Analyze > Analysis Meters Window
(Ctrl+Alt+A), its cards and the menus on them (Meters, Target, the Spectrum's and the
Ambisonics card's Options), and the other meters of the Analyze menu.
-->

# Engineering / Analysis Meters

## The Analysis Meters window

One window with every meter PHI has, laid out as cards that can be hidden from its **Meters**
menu and resized by the dividers between them: Loudness, Loudness History, Levels, Spectrum,
Spectrogram, Imaging, Phase Wheel, Correlation, Amplitude Statistics and Ambisonics. It is
modelled on the Analysis workspace of Adobe Audition and drawn like a mastering meter suite.

It measures what is heard: the audio after the Master Section, from the last **Reset** (R) on,
so the numbers describe the master as the listener will get it, not the raw file. **Freeze** (F)
holds every meter as it is, to read it at leisure. The **Target** menu sets the loudness and
true-peak goal that the loudness gauge and the true-peak readings are judged against. Audio
files dropped on the window are measured from the file itself instead, without the Master
Section, into a written report (see [The analysis report](research:The%20analysis%20report)).

Metering is as old as broadcasting: the VU meter was specified by CBS, NBC and Bell Labs in 1939,
the peak programme meter (PPM) by the BBC and German broadcasters in the 1930s and 40s, and the
loudness meter of ITU-R BS.1770 in 2006. A meter suite brings them together because each sees
something the others miss.

- Eddy Bøgh Brixen, *Audio Metering: Measurements, Standards and Practice* (Focal Press,
  3rd ed. 2020).
- [Peak programme meter](https://en.wikipedia.org/wiki/Peak_programme_meter), Wikipedia.
- [VU meter](https://en.wikipedia.org/wiki/VU_meter), Wikipedia.
- [Adobe Audition](https://en.wikipedia.org/wiki/Adobe_Audition), Wikipedia.

## Loudness units: LUFS, LKFS and LU

**LUFS** (loudness units relative to full scale) is the unit of loudness of ITU-R BS.1770 and EBU
R 128: the mean power of the sound after [K-weighting](research:K-weighting), in decibels, with
an offset of −0.691 dB chosen so that a full-scale 1 kHz sine in one channel reads −3.01 LUFS,
the same as its RMS level. **LKFS** (loudness, K-weighted, relative to full scale) is the same
unit under the ITU's and the American ATSC's name. An **LU** is a difference of loudness, one
decibel's worth: a file at −11 LUFS is 3 LU louder than one at −14.

Unlike a peak or RMS level, a loudness reading is meant to agree with how loud people judge a
sound to be, so that two programmes at the same LUFS sound about equally loud however they
were made. Every loudness reading in PHI uses it: the Loudness card, Loudness History, the Level
Meters panel, Analyze > Loudness Statistics, Normalize to integrated loudness and the Master
Assistant.

See also [Loudness](research:Loudness) for its history.

- [LKFS](https://en.wikipedia.org/wiki/LKFS), Wikipedia.
- ITU-R, [Recommendation BS.1770](https://www.itu.int/rec/R-REC-BS.1770), "Algorithms to
  measure audio programme loudness and true-peak audio level".
- EBU, [Tech 3341](https://tech.ebu.ch/publications/tech3341), "Loudness Metering: EBU Mode
  metering to supplement EBU R 128".

## K-weighting

The filter a signal passes through before its loudness is measured: a high shelf of about +4 dB
above 1.5 kHz, standing in for the way the head and ear canal favour the upper midrange, then a
high-pass filter that rolls off below about 100 Hz, because bass sounds less loud than its power
suggests. The name is simply the letter after the A, B and C weightings of sound level meters.

PHI's meters run it as two biquad filters per channel, as BS.1770 gives them; for surround files
the channels are summed with the standard's weights (the surrounds at 1.41, the LFE left out).

It was chosen from listening tests: Gilbert Soulodre and his colleagues at the Communications
Research Centre in Canada compared many candidate measures against people's judgements of
loudness in the early 2000s, and this simple filter did as well as far more elaborate models of
hearing.

- [LKFS](https://en.wikipedia.org/wiki/LKFS), Wikipedia.
- Gilbert A. Soulodre, "Evaluation of Objective Loudness Meters", AES 116th Convention
  (Berlin, 2004).

## Momentary, short-term and integrated loudness

Three readings of the same measure over different spans. **Momentary** loudness is the mean of
the last 400 ms and follows each word and drum hit; **short-term** is the mean of the last 3 s and
follows phrases and sections; **integrated** is the loudness of everything measured since the
reset, [gated](research:Gating) so that silence and quiet pauses do not pull it down. The maximum
momentary and maximum short-term values are kept too.

PHI measures the K-weighted power in 100 ms blocks: momentary is the mean of the last 4, short-term
of the last 30, and integrated is worked out from 400 ms blocks overlapping by 75 %, as BS.1770
has it. The Loudness card shows integrated loudness on a gauge around the target, with momentary
and short-term beside it; the Level Meters panel shows all three as bars.

The momentary and short-term windows are those of EBU Tech 3341 (2010), which defined the "EBU
Mode" every loudness meter now offers.

- EBU, [R 128](https://tech.ebu.ch/publications/r128), "Loudness normalisation and permitted
  maximum level of audio signals".
- EBU, [Tech 3341](https://tech.ebu.ch/publications/tech3341).
- [EBU R 128](https://en.wikipedia.org/wiki/EBU_R_128), Wikipedia.
- [LKFS](https://en.wikipedia.org/wiki/LKFS), Wikipedia.

## Gating

Leaving out the quiet parts when a whole programme's loudness is measured. BS.1770 uses two
gates: an **absolute gate** at −70 LUFS that drops near silence, then a **relative gate** 10 LU
below the loudness of what is left, so that a film's quiet dialogue scenes or a song's fade-out
do not make the whole seem quieter than its main content.

PHI's integrated readings apply both gates; for the long meters that run for hours only the
blocks above the absolute gate are kept, so silence costs nothing. The [loudness
range](research:Loudness%20range%20%28LRA%29) uses gates of its own.

Gating was added in the second revision of BS.1770 (2011), after the first (2006) had measured
everything alike.

- ITU-R, [Recommendation BS.1770](https://www.itu.int/rec/R-REC-BS.1770), section 2 and
  annex 1.
- [LKFS](https://en.wikipedia.org/wiki/LKFS), Wikipedia, on the gated measurement.

## Loudness range (LRA)

How much the loudness of a programme varies over its length, in LU: the spread of its short-term
loudness values from the 10th to the 95th percentile, after an absolute gate at −70 LUFS and a
relative gate 20 LU below their mean. The percentiles leave out a single loud bang and the
quietest moments, so it describes the dynamics of the music rather than its extremes.

A heavily compressed pop master may have an LRA of 3 to 6 LU, a dynamic orchestral recording 15
LU or more. PHI shows it on the Loudness card, in Amplitude Statistics, in Analyze > Loudness
Statistics and in the report of a dropped file. It is not the same as the
[dynamic range](research:Amplitude%20statistics) of Amplitude Statistics, which compares the
loudest and quietest 50 ms RMS windows.

It was defined by EBU Tech 3342 (2010) to go with R 128, after Esben Skovenborg's work at TC
Electronic on measuring loudness variation.

- EBU, [Tech 3342](https://tech.ebu.ch/publications/tech3342), "Loudness Range: A measure to
  supplement EBU R 128 loudness normalisation".
- Esben Skovenborg, "Loudness Range (LRA): Design and Evaluation", AES 132nd Convention
  (Budapest, 2012).
- [EBU R 128](https://en.wikipedia.org/wiki/EBU_R_128), Wikipedia, on loudness range.
- [Percentile](https://en.wikipedia.org/wiki/Percentile), Wikipedia.

## True peak and dBTP

The highest level the waveform reaches between the samples, as a converter will rebuild it.
Two samples close to full scale can have a peak between them that is higher than either: a
**sample peak** meter, which reads only the samples, misses it, and the overshoot can clip a
digital-to-analogue converter or come out of a lossy encoder (MP3, AAC, Opus) as distortion.
**dBTP** is decibels true peak, against full scale.

PHI's true-peak detector oversamples four times with a 48-tap windowed-sinc interpolation filter
and takes the largest of the in-between values, as BS.1770 annex 2 describes. The Levels card and
the Level Meters panel show true peak beside sample peak; the Loudness card shows the highest
true peak since the reset against the target's ceiling; Normalize can limit the true peak, and the
Master Section's limiter keeps it below a ceiling.

Delivery specifications set a true-peak ceiling because of this: −1 dBTP for EBU R 128 and most
streaming services, −2 dBTP for ATSC A/85 and Amazon Music.

- [Audio normalization](https://en.wikipedia.org/wiki/Audio_normalization), Wikipedia.
- ITU-R, [Recommendation BS.1770](https://www.itu.int/rec/R-REC-BS.1770), annex 2.
- Søren H. Nielsen and Thomas Lund, "0 dBFS+ Levels in Digital Mastering", AES 109th
  Convention (Los Angeles, 2000).

## PLR and PSR

**PLR**, the peak to loudness ratio, is the true peak minus the integrated loudness, in LU: how
far the loudest peak stands above the average loudness. **PSR**, the peak to short-term loudness
ratio, is the true peak minus the maximum short-term loudness. Both are measures of how much
punch and transient detail a master has left: a PLR of 8 LU or below means heavy limiting, 12 LU
or more a master that keeps its peaks.

PHI shows PLR on the Loudness card and both in the report of a dropped file. Under loudness
normalisation a master with more PLR is not turned down more than a squashed one of the same
loudness, so it keeps its peaks without losing loudness.

The PLR is the loudness-era successor of the [crest factor](research:Amplitude%20statistics), which
compares the peak with the RMS level instead of the loudness; Thomas Lund of TC Electronic was
among those who argued, as loudness normalisation spread, that masters should be judged by it.

- AES TD1008, "Recommendations for Loudness of Internet Audio Streaming and On-Demand
  Distribution" (2021).
- Thomas Lund, "Level and Distortion in Digital Broadcasting", *EBU Technical Review* (2007).
- [Crest factor](https://en.wikipedia.org/wiki/Crest_factor), Wikipedia.
- [Loudness war](https://en.wikipedia.org/wiki/Loudness_war), Wikipedia.

## Loudness targets

The integrated loudness and true-peak ceiling a delivery aims at. The Analysis Meters window's
**Target** menu offers:

- **Streaming: −14 LUFS, −1 dBTP**, the reference of Spotify, YouTube and Tidal, which turn
  louder music down to about this level.
- **Apple Music, podcasts: −16 LUFS, −1 dBTP**, Apple's Sound Check and the AES recommendation
  for podcasts.
- **Amazon Music: −14 LUFS, −2 dBTP.**
- **EBU R128 broadcast: −23 LUFS, −1 dBTP**, European radio and television.
- **ATSC A/85 broadcast: −24 LKFS, −2 dBTP**, American television, law under the CALM Act.
- **CD, club: −9 LUFS, −0.3 dBTP**, the loud masters of physical releases and DJ use, where no
  normalisation turns them down.

The loudness gauge colours the reading by its distance from the target, the report of a dropped
file says how far the file is from it and what normalised playback will do, and Normalize and the
Master Assistant offer the same presets. Streaming services change their figures from time to
time; these are the published values PHI was written from.

- EBU, [R 128](https://tech.ebu.ch/publications/r128) (2010, revised 2020).
- ATSC A/85, "Techniques for Establishing and Maintaining Audio Loudness for Digital Television".
- AES TD1008, "Recommendations for Loudness of Internet Audio Streaming and On-Demand
  Distribution" (2021).
- [Audio normalization](https://en.wikipedia.org/wiki/Audio_normalization), Wikipedia.
- [EBU R 128](https://en.wikipedia.org/wiki/EBU_R_128), Wikipedia.
- [CALM Act](https://en.wikipedia.org/wiki/Commercial_Advertisement_Loudness_Mitigation_Act), Wikipedia.

## Loudness History

Short-term and momentary loudness drawn over time, from 30 seconds to 10 minutes back (the
span is chosen on the card), coloured by how far each moment is from the target, with the
integrated value marked. It shows what single numbers hide: a chorus that jumps, a quiet
intro, a section pushed too hard.

The loudness history graph, sometimes drawn as a radar, is part of most loudness meters since
EBU R 128; it is the loudness counterpart of a level recorder's paper chart.

- EBU, [Tech 3341](https://tech.ebu.ch/publications/tech3341), section on visualisation.
- [EBU R 128](https://en.wikipedia.org/wiki/EBU_R_128), Wikipedia.

## Peak and RMS levels

The **peak** level is the largest sample value, in dBFS. The **RMS** (root mean square) level is
the square root of the mean of the squared samples over a short time: a measure of the signal's
power, closer to how loud it sounds than its peak. A sine's RMS is 3 dB below its peak, a square
wave's equal to it, speech and music 10 to 20 dB below.

The Levels card shows both for left and right. The peak bar falls back at 24 dB a second, and a
hold line stays at the highest peak for two seconds before it falls; RMS is averaged over about
300 ms and shown, by the AES17 convention, 3 dB higher, so that a sine's RMS reads the same as its
peak. A true-peak reading and a clip counter sit at the top of each bar.

These ballistics, how fast a meter rises and falls, are what distinguished the old meters: the
VU meter was an RMS-like average over 300 ms, the PPM a quick peak with a slow fall.

- [Root mean square](https://en.wikipedia.org/wiki/Root_mean_square), Wikipedia.
- [Peak programme meter](https://en.wikipedia.org/wiki/Peak_programme_meter), Wikipedia.
- [VU meter](https://en.wikipedia.org/wiki/VU_meter), Wikipedia.
- AES17, "AES standard method for digital audio engineering — Measurement of digital audio
  equipment".

## Clipping

What happens when a signal is asked to go beyond the largest value a system can hold: the tops
of the waveform are cut flat. In analogue equipment it is gradual; in fixed-point digital audio
it is abrupt and adds harsh, inharmonic distortion (aliased harmonics among them).

PHI counts a sample as clipped when it reaches full scale (within about −0.0003 dBFS). The
Levels card lights its clip indicator with a count, Amplitude Statistics lists "possibly clipped
samples" (possibly, because a loud sample at full scale can be legitimate), the Diagnostics panel
lists clipped stretches, and Repair > DeClip rebuilds them. Inside PHI audio is floating point
and does not clip until it is saved or played, which is why a true-peak limiter belongs at the
end of the Master Section.

- [Clipping (audio)](https://en.wikipedia.org/wiki/Clipping_(audio)), Wikipedia.

## Spectrum analyser

A display of how much energy the sound has at each frequency, now: level in decibels up the
side, frequency across on a logarithmic scale from 20 Hz to 20 kHz, so that each octave takes
the same width. The Spectrum card works out an [FFT](research:The%20Fourier%20transform%20and%20the%20FFT)
of the newest samples at every screen refresh and smooths each bin over time.

Its controls are the terms of the following entries: which channels (Left & Right, Mid & Side,
Average, or one alone), [smoothing](research:Fractional-octave%20smoothing),
[slope](research:Spectral%20slope), [FFT size and zero padding](research:FFT%20size,%20resolution%20and%20zero%20padding),
[window](research:Window%20functions), [estimate](research:Multitaper%20estimate),
[response](research:Response%20and%20averaging), range (60, 96 or 120 dB) and display (lines,
filled or bars), and the [holds](research:Peak%20hold%20and%20spectrum%20holds). The note under
the mouse and the strongest frequency of each side are shown as you read it.

It follows Audition's Frequency Analysis panel. Spectrum analysers were swept-filter instruments
until the 1970s; Hewlett-Packard's FFT analysers of that decade and the personal computers of the
1990s made them part of every studio.

- [Spectrum analyzer](https://en.wikipedia.org/wiki/Spectrum_analyzer), Wikipedia.
- Bob Metzler, *Audio Measurement Handbook* (Audio Precision, 1993).

## Fractional-octave smoothing

Averaging the spectrum over a band around each frequency whose width is a fixed fraction of an
octave: 1/24, 1/12, 1/6, 1/3 or a whole octave. Because the FFT's bins are evenly spaced in
hertz, a raw spectrum has a few bins per octave in the bass and thousands in the treble, which
makes the top end a dense, jittery forest; smoothing in fractions of an octave matches the
resolution to how hearing groups frequencies, so a broad tilt or a resonance can be read at a
glance. Finer smoothing shows single notes and hum; coarser shows the tonal balance.

PHI averages the power (not the decibels) over the band, from running sums, so smoothing costs
the same at any width. The Spectrum card's smoothing menu offers None to 1 octave; 1/6 octave is
the default.

The third octave is the old standard of acoustics, the band of the graphic equalisers and real-time
analysers of the 1970s (IEC 61260); the finer fractions came with FFT analysis.

- [Octave band](https://en.wikipedia.org/wiki/Octave_band), Wikipedia.
- Panagiotis D. Hatziantoniou and John N. Mourjopoulos, "Generalized Fractional-Octave
  Smoothing of Audio and Acoustic Responses", *JAES* 48 (2000).

## Spectral slope

A tilt added to the spectrum display so that a balanced sound reads flat. Music and pink noise
have about equal energy per octave, so their power per hertz falls by 3 dB an octave, and on an
untilted FFT display they slope down to the right. The Spectrum card's **Slope** options:
**Flat** (no tilt, white noise reads flat), **3 dB per octave** (pink noise reads flat, the
default) and **4.5 dB per octave** (a typical finished mix reads roughly flat, as several
mastering analysers set it).

With the slope chosen, what sticks up is too loud for a balanced mix and what dips is too quiet;
the card also states the slope of what it measures.

- [Pink noise](https://en.wikipedia.org/wiki/Pink_noise), Wikipedia.
- Pietro Pestana and others, "Spectral Characteristics of Popular Commercial Recordings
  1950-2010", AES 135th Convention (New York, 2013).

## FFT size, resolution and zero padding

The **FFT size** is how many samples each transform takes, from 1024 to 32 768 in the Spectrum
card's Options. A larger frame separates frequencies more finely, each bin being the sample rate
divided by the size apart (5.9 Hz at 8192 points and 48 kHz), but it covers more time, so it
reacts more slowly and smears short sounds. That trade between frequency and time resolution is
unavoidable; it is the uncertainty principle of signal analysis.

**Zero padding** (2×, 4×, 8×) adds zeros after the samples before the transform. It draws the
spectrum at more points, between the bins, so peaks look smoother and can be read more exactly,
but it does not separate two close tones any better than the frame itself does.

- [Spectral leakage](https://en.wikipedia.org/wiki/Spectral_leakage), Wikipedia.
- Julius O. Smith III, [*Spectral Audio Signal Processing*](https://ccrma.stanford.edu/~jos/sasp/)
  (W3K Publishing, 2011), "Zero Padding".

## Window functions

A frame of samples cut out of a longer sound has abrupt edges, and the transform reads those edges
as energy at frequencies that are not in the sound: **leakage**, a skirt of **sidelobes** around
every tone. A window fades the frame in and out before the transform. Each shape trades the width
of the main peak (how close two tones can be and still be told apart) against how far down its
sidelobes are (how quiet a sound beside a loud one can be and still be seen).

The Spectrum card's Window menu has twenty: **Hann** (the general choice), **Blackman-Harris**
(sidelobes 92 dB down, the default, for low-level detail), **Flat Top** (exact levels of tones
at the cost of wide peaks, for calibration), **Hamming**, **Rectangular** (no window: sharpest
peaks, most leakage), **Triangular**, **Blackman**, **Welch**, **Kaiser** at 140 and 180 dB,
**Nuttall**, **Blackman-Nuttall**, **Gaussian**, **Tukey** (flat in the middle), **Dolph-Chebyshev**
(every sidelobe at the same height, 120 dB down), **Bartlett-Hann**, **Bohman**, **Parzen**,
**Lanczos** and **Exponential**. The spectrogram of the editor offers Hann, Hamming and
Blackman-Harris.

Most carry the names of the people who described them: Julius von Hann (a meteorologist, for
smoothing weather data), Richard Hamming and James Kaiser of Bell Labs, Charles Dolph (for
antenna arrays, 1946), Albert Nuttall (1981). Fredric Harris's survey of 1978 set out their
properties side by side and is still the reference.

- [Window function](https://en.wikipedia.org/wiki/Window_function), Wikipedia.
- Fredric J. Harris, "On the Use of Windows for Harmonic Analysis with the Discrete Fourier
  Transform", *Proceedings of the IEEE* 66 (1978).
- Albert H. Nuttall, "Some Windows with Very Good Sidelobe Behavior", *IEEE Transactions on
  Acoustics, Speech, and Signal Processing* 29 (1981).

## Multitaper estimate

A way to estimate a spectrum with less randomness than a single windowed FFT. Instead of one
window, each frame is transformed several times, each with a different **taper** from a family
of mutually independent shapes, the discrete prolate spheroidal sequences (DPSS or Slepian
sequences); the spectra are averaged. Each taper sees the same frequencies with almost no
leakage, and their average has a far steadier noise floor than any one of them.

PHI's **Multitaper (7 DPSS tapers)** estimate, in the Spectrum card's Options under Estimate, uses
seven tapers with a time-bandwidth product of 4, so each tone is about eight bins wide; the Window
setting is not used. It suits reading noise floors and broad spectra, where the plain FFT's
jitter gets in the way.

David Slepian and Henry Pollak found the sequences at Bell Labs in 1961; David Thomson, also of
Bell Labs, made them a method of spectrum estimation in 1982.

- [Multitaper](https://en.wikipedia.org/wiki/Multitaper), Wikipedia.
- David J. Thomson, "Spectrum Estimation and Harmonic Analysis", *Proceedings of the IEEE* 70
  (1982).
- Donald B. Percival and Andrew T. Walden, *Spectral Analysis for Physical Applications*
  (Cambridge University Press, 1993).

## Response and averaging

How fast the spectrum follows the music. **Fast** (40 ms), **Medium** (150 ms) and **Slow** (600 ms)
average each bin's power exponentially over that time: fast to watch single notes, slow to see the
balance of a passage. **Infinite** counts every frame since the reset alike, which is Welch's
method of averaged periodograms: a steady long-term spectrum of the whole piece, and an exact
reading of a noise floor.

Peter Welch published the method in 1967; it is still the standard way to estimate the spectrum
of a long signal from short transforms.

- [Welch's method](https://en.wikipedia.org/wiki/Welch%27s_method), Wikipedia.
- Peter D. Welch, "The Use of Fast Fourier Transform for the Estimation of Power Spectra",
  *IEEE Transactions on Audio and Electroacoustics* 15 (1967).

## Peak hold and spectrum holds

The **Peak Hold Curve** (Spectrum Options) draws, above the live spectrum, the highest level each
frequency has reached since the reset: where the loudest resonances, the hum and the harshest
moments were. The eight **holds** (the buttons 1 to 8, or the keys) freeze the spectrum as it is
now as a coloured trace, so that a verse can be laid over a chorus, a mix over a reference, or the
sound before an equaliser over the sound after it. **Clear All Holds** removes them.

Overlaying spectra is the heart of matching a mix to a reference, the method of tools such as
the Master Assistant and of "match EQ".

- [Spectrum analyzer](https://en.wikipedia.org/wiki/Spectrum_analyzer), Wikipedia.

## Spectrogram meter

The spectrum drawn over time: each moment a column, frequency up the side, level as colour, the
newest on the right. The Spectrogram card scrolls with what plays, from −90 to −12 dB across its
colours; it shows how the sound develops, where the spectrum card shows only now. The editor's
[spectral display](research:Spectral%20display) draws the same picture of the whole file.

- [Spectrogram](https://en.wikipedia.org/wiki/Spectrogram), Wikipedia.

## Goniometer and the imaging views

A display of the stereo image. The **goniometer** plots each pair of left and right samples as a
point, turned 45° so that the mid points straight up and the side across: a mono sound is a
vertical line, a wide stereo sound a round cloud, a sound in one channel a diagonal, and an
out-of-phase sound a horizontal line. The Imaging card (and the Meters panel of the main window)
has six views, changed by clicking or with Analyze > Next Imaging View:

- **Goniometer**, as above.
- **Polar Sample**: each sample a dot in a half circle, its angle the stereo position, its distance
  the level. Dots within the 45° lines are mono-safe.
- **Polar Level**: a ray for each direction, as long as the loudest sound heard there, with an
  outline that slowly contracts.
- **Lissajous**: the classic oscilloscope trace, left against right in a diamond; it should stay
  taller than wide.
- **Binaural**: the head from above, with where each frequency band seems to come from, judged
  from the time and level differences between the ears.
- **Mid/Side Levels**: the mid and side as two meters, the side against the mid, and that
  difference over the last ten seconds.

Points glow and fade like the phosphor of an oscilloscope tube, which is what broadcast
goniometers were. Lissajous figures carry the name of Jules Antoine Lissajous, who studied them
in 1857 with mirrors on vibrating tuning forks.

- [Stereo imaging](https://en.wikipedia.org/wiki/Stereo_imaging), Wikipedia.
- [Lissajous curve](https://en.wikipedia.org/wiki/Lissajous_curve), Wikipedia.

## Phase correlation

How alike the left and right channels are, from −1 to +1: the normalised cross-correlation, the
mean of L × R divided by the square root of the mean of L² times the mean of R². At **+1** the
two are the same (mono); near **0** they are unrelated (very wide, or two different sounds); below
0 they are partly opposite, and at **−1** one is the other upside down, which cancels completely
when the two are summed to mono.

A mix whose correlation stays between about 0 and +1 is safe in mono, on a phone speaker or a
club's summed system; one that dips below 0 loses part of its sound there. PHI smooths it over
about 300 ms on the Correlation card and under the goniometer, and the report of a dropped file
gives the whole file's value with a word on what it means.

- [Cross-correlation](https://en.wikipedia.org/wiki/Cross-correlation), Wikipedia.
- Eddy Bøgh Brixen, *Audio Metering: Measurements, Standards and Practice* (Focal Press,
  3rd ed. 2020), on correlation meters.

## Stereo balance

How much louder one channel is than the other, in dB, over about 300 ms: "centre" when they are
within 0.05 dB. The Correlation card shows it as a bar between L and R, and the report of a dropped
file gives the whole file's balance. A mix that leans to one side for its whole length usually has
a panning or channel-gain fault.

- [Panning (audio)](https://en.wikipedia.org/wiki/Panning_(audio)), Wikipedia.

## Phase Wheel

Where each frequency sits in the stereo field, in 1/24-octave bands: drawn on a circle with mono
at the top, left and right at the sides and fully out of phase (inverse) at the bottom, each band
a point at the angle given by its mid and side power, nearer the edge when louder. A healthy mix
gathers in the upper half, its bass near the top; bands drifting towards the bottom are where the
channels cancel in mono.

It is PHI's own view, after the frequency-resolved correlation of mastering imagers.

- [Stereo imaging](https://en.wikipedia.org/wiki/Stereo_imaging), Wikipedia.

## Correlation per band

The [phase correlation](research:Phase%20correlation) worked out separately for each third
octave from 20 Hz to 20 kHz, from the cross spectrum of left and right, with the overall
correlation and the balance beside it. It shows what a single correlation hides: wide, uncorrelated
cymbals above a mono kick, or a bass whose stereo widening cancels in mono while everything else
is fine. Bass should usually stay near +1.

- [Coherence (signal processing)](https://en.wikipedia.org/wiki/Coherence_(signal_processing)),
  Wikipedia.

## Amplitude statistics

A table of numbers for each channel of everything heard since the reset, as Audition's Amplitude
Statistics panel has them; **Copy** puts them on the clipboard as text.

- **Peak amplitude**: the largest sample, in dB.
- **True peak amplitude**: the largest [true peak](research:True%20peak%20and%20dBTP), in dBTP.
- **Maximum and minimum sample value**: the highest and lowest sample, as numbers from −1 to 1. A
  waveform that goes further one way than the other is asymmetric (some voices, brass, or a DC
  offset).
- **Possibly clipped samples**: samples at full scale; see [Clipping](research:Clipping).
- **DC offset**: the mean of the samples, as a percentage of full scale; see
  [DC offset](research:DC%20offset).
- **Total RMS amplitude**: the RMS level of everything at once.
- **Maximum, minimum and average RMS amplitude**: of the 50 ms windows the sound is cut into
  (digital silence left out).
- **Dynamic range**: the maximum RMS minus the minimum RMS: how far the loudest moment is from the
  quietest.
- **Crest factor**: the peak minus the total RMS, in dB: how spiky the waveform is. A sine has 3
  dB, a heavily limited master 6 to 8, an unprocessed drum recording 20 or more.

Below them an **RMS histogram** shows how much of the time the 50 ms RMS level spent at each
decibel from −60 to 0, and the integrated loudness, loudness range and maximum momentary and
short-term loudness follow.

- [Crest factor](https://en.wikipedia.org/wiki/Crest_factor), Wikipedia.
- [Dynamic range](https://en.wikipedia.org/wiki/Dynamic_range), Wikipedia.

## The Ambisonics card

Where the sound is in an ambisonic file, up to 10th order, read from the file at the playhead
rather than from what plays: a sphere, four flat views, a panorama of the whole sphere, the head
from above and the side, energy plots, a scope with the speakers of a layout, a bar per speaker,
and a bar per ambisonic channel. Its Options set the range, whether levels are absolute or
relative to the loudest place, the response, the highest order shown, the speaker layout, and
whether unmarked 4, 9 and 16 channel files count as Ambisonics (first, second and third order).

See [Ambisonics](research:Ambisonics) for what the format is.

- [Ambisonics](https://en.wikipedia.org/wiki/Ambisonics), Wikipedia.
- Franz Zotter and Matthias Frank, *Ambisonics: A Practical 3D Audio Theory for Recording,
  Studio Production, Sound Reinforcement, and Virtual Reality* (Springer, 2019).

## The analysis report

A written report of a whole file, made when audio files are dropped on the Analysis Meters
window. It is measured from the file itself, not through the Master Section, and gives: the
format; integrated loudness, loudness range, maximum momentary and short-term loudness, true and
sample peak, PLR and PSR; how far it is from the chosen target and what normalised playback will
do to it; the amplitude statistics; the stereo correlation, balance and side against mid; the
strongest frequencies; the RMS histogram; and the average spectrum in third octaves with the
correlation of each band. Surround files are measured with every channel for loudness and folded
down to stereo, as PHI plays them, for the rest.

A report like this is what a mastering engineer checks before delivery, and what a quality
control department checks on arrival.

- EBU, [R 128](https://tech.ebu.ch/publications/r128).
- [Audio mastering](https://en.wikipedia.org/wiki/Audio_mastering), Wikipedia.
- [Quality control](https://en.wikipedia.org/wiki/Quality_control), Wikipedia.

## Level Meters and Loudness Statistics

Two lighter measurements in the Analyze and View menus. **View > Level Meters** (Ctrl+Shift+L)
is a panel beside the editor with a sample-peak and true-peak bar per channel, with peak hold, and
momentary, short-term and integrated loudness; **Analyze > Reset Meters** starts it again, with
the goniometer and the Analysis Meters window. **Analyze > Loudness Statistics** (Ctrl+Shift+A)
measures the selection, or the whole file, from the samples themselves: integrated loudness,
loudness range, true peak and sample peak.

The difference matters: the meters measure what plays, through the Master Section, while
Loudness Statistics and Normalize measure the file as it is.

- ITU-R, [Recommendation BS.1770](https://www.itu.int/rec/R-REC-BS.1770).
- [Peak meter](https://en.wikipedia.org/wiki/Peak_meter), Wikipedia.
- [LKFS](https://en.wikipedia.org/wiki/LKFS), Wikipedia.
