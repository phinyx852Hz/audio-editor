# Meters and Analysis

PHI measures in three places. The **Meters panel** sits beside the waveform while you work. The **Analysis Meters window** is a full meter suite of its own, for mastering. And the **Analyze menu** measures files and finds beats, onsets, tempo and key.

## The Meters panel {#meters-panel}

[]{.ix t="Meters panel"}[]{.ix t="Level meters"}View > Level Meters (`Ctrl+Shift+L`) shows, from top to bottom:

- a **stereo imaging display**, with six views;
- a **phase correlation meter** from −1 to +1;
- **sample peak bars** with peak hold, and the **true peak**;
- **momentary, short-term and integrated loudness** (ITU-R BS.1770 / EBU R128) during playback.

![The Meters panel during playback: the goniometer, correlation, peaks and loudness.](images/meters-panel.png)

`Ctrl+Alt+R` resets the meters. They measure what you hear, after the Master Section.

### The imaging views

[]{.ix t="Goniometer"}[]{.ix t="Imaging display"}Click the display, or press `Ctrl+Alt+V`, for the next view.

| View | Shows |
|---|---|
| Goniometer | Mid up, side across, as a glowing phosphor trace |
| Polar Sample, Polar Level | Half-circle views in the style of iZotope Insight; inside the 45° lines is mono-safe |
| Lissajous | The classic diamond, zooming in by itself on quiet material |
| Binaural | The head from above, showing where each frequency band seems to come from, judged from the time and level differences between the ears; faint, spread dots are room sound |
| Mid/Side Levels | Mid and side meters in dBFS with peak hold, and the side against the mid now and over the last 10 seconds |

### Reading the correlation meter

[]{.ix t="Correlation meter"}+1 means left and right are the same: mono. Around +0.3 to +0.8 is a healthy stereo mix. Near 0 the sides are unrelated, very wide. Below 0, parts of the sound are out of phase and will thin out or vanish when played in mono, on a phone speaker for example.

## Loudness Statistics

[]{.ix t="Loudness Statistics"}Analyze > Loudness Statistics (`Ctrl+Shift+A`) measures the selection or file: integrated loudness (LUFS), loudness range (LU) and true peak (dBTP).

## The Analysis Meters window {#analysis-meters}

[]{.ix t="Analysis Meters window"}Analyze > Analysis Meters Window (`Ctrl+Alt+A`) is a mega meter suite in its own window, with Adobe Audition's analysis detail and the look of a mastering meter.

![The Analysis Meters window: loudness gauge, history, levels and spectrum.](images/analysis-meters.png)

| Meter | Shows |
|---|---|
| Loudness | A gauge around a target you pick: short-term inside, momentary outside, integrated in the middle, with max momentary and short-term, loudness range, true peak and PLR |
| Loudness History | 30 s to 10 min, coloured by how far from the target, with the integrated line |
| Levels | Peak, RMS, peak hold, true peak and clip counts for left and right; the menu on the card switches the count between every full-scale sample (Clips) and OVERs, runs of 3 or more full-scale samples counted once each |
| Spectrum | Like Audition's Frequency Analysis (see below) |
| Spectrogram | Scrolling |
| Imaging | The goniometer with all its views |
| Phase Wheel | Each frequency between mono, left, right and inverse |
| Correlation | Per third octave, with the overall correlation and balance |
| Amplitude Statistics | Peak, true peak, sample values, possibly clipped samples, OVERs, DC offset, total, maximum, minimum and average RMS, dynamic range, crest factor, with an RMS histogram and Copy |
| Bits | A bitscope: each bit of the left and right samples, from the sign bit to the 16th, 20th or 24th (chosen on the card), filled by how often it is set and outlined while it moves, with how many bits are in use and how much of the signal is finer than the chosen depth |
| Classic Meters | The meters of the analogue studio, on what PHI plays: a VU meter (300 ms, after the 1940 specification), the four quasi-peak programme meters as needles, EBU (Type IIb, marked -12 to +12 around TEST), BBC (Type IIa, 1 to 7), DIN (Type I, -40 to +5) and Nordic (Type I, -36 to +12), and Bob Katz's K-20, K-14 and K-12 as bars (average green to 0, amber to +4, red above, with the peak and a 10 s peak hold). The needles read around the line-up tone, -18 dBFS (EBU R 68) or -20 dBFS (SMPTE RP 155), chosen on the card: it reads 0 VU, TEST, 4 on the BBC meter and -9 on the DIN meter. The BBC meter's fall is taken from the EBU meter's (24 dB in 2.8 s) until IEC 60268-10 confirms it |
| Ambisonics | Where the sound is in an ambisonic file (see below) |

**Loudness targets**: Streaming −14, Apple Music and podcasts −16, Amazon −14 at −2 dBTP, EBU R128 −23, ATSC A/85 −24, CD and club −9.

**Spectrum** options: Left & Right, Mid & Side, Average or one channel; lines, filled or bars; 1/24 to 1-octave smoothing; a 3 or 4.5 dB per octave slope (so a balanced mix looks flat); FFT size 1024 to 32768, zero padding up to 8x (smoother peaks between bins), a windowed FFT or a multitaper estimate (seven DPSS tapers, a steadier noise floor), Fast, Medium, Slow or Infinite response (every frame since Reset alike, as Welch's method averages), and 22 windows (Hann, Blackman-Harris, Flat Top (the Stanford Research SR785's), Hamming, Rectangular, Triangular, Blackman, Welch, Kaiser 140 and 180 dB, Nuttall, Blackman-Nuttall, Gaussian, Tukey, Dolph-Chebyshev 120 dB, Bartlett-Hann, Bohman, Parzen, Lanczos, Exponential, and Heinzel's flat tops HFT95 and HFT90D, each with a tooltip on what it is for; levels stay corrected for each window); peak hold; eight hold traces in the Selector's colours, Silver to Ruby (`1` to `8`); the frequency, note and level under the mouse; and the strongest frequency and its note for each side.

Hide meters you do not need from the **Meters** button, and drag the dividers between them. Both are remembered.

| Keys | Does |
|---|---|
| `Space` | Plays or stops |
| `R` | Resets |
| `F` | Freezes |
| `F11` | Full screen |
| `V` | Next ambisonic view |
| `Esc`, `Ctrl+W`, `Super+W` | Closes the window |

### Ambisonics views

[]{.ix t="Ambisonics!meters"}For ambisonic files (AmbiX, first to tenth order, like REAPER's), the Ambisonics meter shows where the sound is, from all the file's channels at the playhead, in eight views after the best ambisonic meters and panners:

![The ambisonic views of the Analysis Meters window.](images/ambisonics-views.png)

| View | Shows |
|---|---|
| Sphere | A glass globe with a heat map, turned by dragging or the arrows, brought closer with the wheel or `Ctrl+Up`/`Ctrl+Down` |
| Four Views | Left, front, top and the globe |
| Panorama | The whole sphere on an equal-area map |
| Head | The listener from above and from the side, with the energy around them |
| Energy | Polar plots from above, behind and the right, direction meters and an energy pad with height |
| Scope | A glowing cloud over a 7.1.4 layout, with a height switch |
| Speakers | A bar with peak hold for each speaker of a layout you pick (stereo to 9.1.6) beside the surround energy |
| Channels | A bar for each ambisonic channel, grouped by order (4 at first order, 121 at tenth) |

It gives the direction and level under the mouse, the loudest direction, and how diffuse the sound is. Files marked Ambisonics in Spatial > Channel Layout, and files of 25 channels or more, count as ambisonic.

### Reports on files

[]{.ix t="Analysis report"}Drop audio files on the Analysis Meters window (from a file manager or the Find panel) for a full report of each, measured from the file itself: loudness against the target, amplitude statistics, stereo, an RMS histogram and the average spectrum in third octaves. Nothing is saved until you say: `Ctrl+S` saves the report in `~/Documents/PHI Analysis` (or a folder you choose), and closing an unsaved report asks first.

## Analysis with Vamp plugins {#vamp}

[]{.ix t="Vamp plugins"}[]{.ix t="Beats!marking"}[]{.ix t="Tempo!detecting"}[]{.ix t="Key detection"}As in Sonic Visualiser, PHI runs Vamp analysis plugins on the selection or the whole file.

| Command | Keys | Does |
|---|---|---|
| Mark Beats | `Ctrl+Alt+B` | A marker on every beat, named by bar and beat ("Bar 3", "Beat 3.2") |
| Mark Onsets | `Ctrl+Alt+Shift+O` | A marker at every hit or note start |
| Detect Tempo and Key | `Ctrl+Alt+Shift+T` | Finds the BPM and key and writes them to the tags |
| Vamp Plugins | `Ctrl+Alt+Shift+V` | Lists every Vamp plugin found and runs one; its results become markers, or regions when they have a length |

The QM Vamp Plugins (beat, bar, tempo, onset, key and more) come with PHI on x86-64. Other Vamp plugins are found in `~/vamp`, `~/.vamp`, `/usr/local/lib/vamp`, `/usr/lib/vamp` or `VAMP_PATH`.
