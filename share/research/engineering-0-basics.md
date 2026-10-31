<!--
Research > Engineering (owner, 2026-10-31): the audio engineering behind PHI's own menus,
each term what it is, how PHI measures or does it, and where it comes from, with links for
further reading. Only what PHI offers: a term belongs here when a menu, window or dialog of
PHI uses it. The files are numbered in the order of the submenus: 0 the basics, 1 Analysis
Meters, 2 Plugin Analyzer, 3 Spectral view, 4 Process and its Effect submenu, 5 Nyquist,
6 Restoration.
-->

# Engineering

## Audio engineering

The craft of recording, shaping, measuring and delivering sound with technical means: microphones
and converters, editing, processing, mixing, mastering, restoration and the measurement that keeps
each step honest. It sits between physics (what the air and the signal do), electronics and
signal processing (what the equipment does to them) and listening (what people hear).

PHI's menus follow the stages of that work. Edit and Process change the samples themselves;
Analyze measures them (the Analysis Meters window, the Plugin Analyzer, Loudness Statistics);
View > Spectral Display shows them in time and frequency at once; Repair restores them; Master
runs them through a chain of plugins on the way out; Nyquist lets anyone write a process of their
own. The entries in this menu explain the terms those menus use.

The profession grew out of telephone and radio engineering in the 1920s, when Bell Telephone
Laboratories and the broadcasters needed ways to measure and keep levels; the recording engineer
became a role of its own with electrical recording (1925), magnetic tape (from the late 1940s)
and multitrack recording (from the 1960s), and the digital audio workstation brought all of its
stages onto one screen from the late 1980s.

- [Audio engineer](https://en.wikipedia.org/wiki/Audio_engineer), Wikipedia.
- [Audio signal processing](https://en.wikipedia.org/wiki/Audio_signal_processing), Wikipedia.
- Bob Katz, *Mastering Audio: The Art and the Science* (Focal Press, 3rd ed. 2015).
- Glen Ballou (ed.), *Handbook for Sound Engineers* (Focal Press, 5th ed. 2015).

## Decibels, dBFS and dB

The decibel is a tenth of a bel: ten times the logarithm of a ratio of powers, or twenty times
the logarithm of a ratio of amplitudes (power goes as the square of amplitude). Doubling the
amplitude is +6.02 dB, doubling the power +3.01 dB, and ten times the amplitude +20 dB. A
decibel is always against something: dB SPL against the threshold of hearing, dBu against
0.775 volts, and in digital audio **dBFS**, decibels against digital full scale, where 0 dBFS is
the largest value a sample can hold and everything real is below it.

PHI reads levels in dBFS throughout: the waveform's level colours, Gain and Normalize, every
meter of the Analysis Meters window. A plain "dB" in PHI is a change (Gain Up 0.5 dB) or a
difference between two levels (a crest factor, a side level against the mid). Silence has no
level in decibels at all; PHI shows it as "-inf" or as the bottom of the scale.

The unit comes from Bell Telephone Laboratories, which measured telephone line losses in
"transmission units" and renamed them decibels in honour of Alexander Graham Bell in the late
1920s.

- [Decibel](https://en.wikipedia.org/wiki/Decibel), Wikipedia.
- [dBFS](https://en.wikipedia.org/wiki/DBFS), Wikipedia.
- W. H. Martin, "Decibel: the name for the transmission unit", *Bell System Technical
  Journal* 8 (1929).

## Sample rate and the Nyquist frequency

Digital audio is a series of measurements of the waveform, taken at a fixed rate: 44 100 per
second for the CD, 48 000 for film, video and most streaming, 96 000 or 192 000 for high
resolution work. The **sampling theorem** says that a signal containing nothing above half the
sample rate can be rebuilt exactly from its samples. That half is the **Nyquist frequency**:
22 050 Hz at 44.1 kHz, 24 000 Hz at 48 kHz.

Anything above it that reaches the converter, or that a process creates (the harmonics of
distortion, the corners of a raw square wave), does not vanish: it folds back below the Nyquist
frequency as a tone that was never there, which is [aliasing](research:Aliasing). That is why
PHI's tone generator is band-limited by default, why its resampler filters before it changes
the rate, and why the Plugin Analyzer measures aliasing at all.

The theorem has several parents. Harry Nyquist of Bell Labs showed in 1928 how many pulses a
telegraph line of a given bandwidth can carry; E. T. Whittaker (1915) and Vladimir Kotelnikov
(1933) had the interpolation formula; Claude Shannon stated it for communication in 1949.

- [Nyquist–Shannon sampling theorem](https://en.wikipedia.org/wiki/Nyquist%E2%80%93Shannon_sampling_theorem),
  Wikipedia.
- [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency), Wikipedia.
- Claude E. Shannon, "Communication in the Presence of Noise", *Proceedings of the IRE* 37
  (1949).

## Bit depth and quantization

Each sample is stored as a number of fixed precision. At 16 bits there are 65 536 steps from the
bottom of the scale to the top, at 24 bits about 16.8 million; PHI edits in 32-bit floating
point, which has 24 bits of precision at every level and room far above 0 dBFS, so nothing is
lost between edits and nothing clips until the file is saved in a fixed-point format.

Rounding each value to the nearest step is **quantization**, and the rounding error is noise:
each bit is worth about 6 dB of dynamic range, so 16 bits give about 96 dB between full scale and
the noise and 24 bits about 144 dB. On quiet, simple sounds the error is not random but follows
the signal, and is heard as distortion; [dither](research:Dither) turns it back into plain
noise. Process > Convert Sample Type and the Export dialog reduce bit depth, with or without
dither.

- [Quantization (signal processing)](https://en.wikipedia.org/wiki/Quantization_(signal_processing)),
  Wikipedia.
- [Audio bit depth](https://en.wikipedia.org/wiki/Audio_bit_depth), Wikipedia.
- John Watkinson, *The Art of Digital Audio* (Focal Press, 3rd ed. 2001).

## The Fourier transform and the FFT

Any sound can be described as a sum of sine waves, each with its frequency, amplitude and
phase. The Fourier transform takes the samples of a stretch of sound and gives that description;
the inverse transform turns it back into samples. The **discrete Fourier transform** of N samples
gives N/2 + 1 frequencies, the **bins**, evenly spaced from 0 Hz to the Nyquist frequency, each
the sample rate divided by N apart.

The **fast Fourier transform** (FFT) computes it in a number of steps that grows as N log N
instead of N², which is what makes real-time spectrum analysers possible. Nearly everything in
PHI that speaks of frequency runs on it: the Spectrum and Spectrogram meters, the editor's
spectral display, the Plugin Analyzer, noise reduction, Auto Heal, DeEss and spectral editing.

Joseph Fourier claimed in his *Théorie analytique de la chaleur* (1822) that any function could
be written as such a sum. Gauss had found the fast algorithm around 1805 for the orbits of
asteroids, unpublished in his lifetime; James Cooley and John Tukey found it again in 1965.

- [Fourier transform](https://en.wikipedia.org/wiki/Fourier_transform), Wikipedia.
- [Fast Fourier transform](https://en.wikipedia.org/wiki/Fast_Fourier_transform), Wikipedia.
- James W. Cooley and John W. Tukey, "An Algorithm for the Machine Calculation of Complex
  Fourier Series", *Mathematics of Computation* 19 (1965).
- Julius O. Smith III, [*Mathematics of the Discrete Fourier Transform*](https://ccrma.stanford.edu/~jos/mdft/)
  (W3K Publishing, 2007).

## Mono, stereo, mid and side

Mono is one channel; stereo is two, left and right, played on two speakers so that sounds can
sit anywhere between them. The same two channels can be described another way: the **mid**,
(L + R) ÷ 2, is what the two have in common, and the **side**, (L − R) ÷ 2, is how they differ.
Left is mid plus side, right is mid minus side, so nothing is lost by the change.

The side is what makes stereo wide, and also what disappears when the two channels are added
together for a mono speaker. Many of PHI's meters and processes work in these terms: the
Spectrum meter's Mid & Side view, the Goniometer (mid up, side across), the Mid/Side Levels
view, Process > Mid/Side Width and Stereoize, which adds only side so that the mono sum stays
exactly as it was.

Alan Blumlein's British patent of 1931, the founding document of stereo, already described sum
and difference signals; mid-side microphone technique, a forward-facing microphone for the mid
and a sideways figure of eight for the side, follows from it.

- [Stereophonic sound](https://en.wikipedia.org/wiki/Stereophonic_sound), Wikipedia.
- [Microphone practice](https://en.wikipedia.org/wiki/Microphone_practice), Wikipedia, on M/S
  technique.
- Alan Blumlein, British patent 394,325, "Improvements in and relating to Sound-transmission,
  Sound-recording and Sound-reproducing Systems" (1931).
