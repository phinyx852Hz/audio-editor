<!--
Research > Engineering > Spectral view: the terms of View > Spectral Display (Shift+D), the
blend between waveform and spectrogram, the Pitch Line, and View > Spectrogram Settings.
-->

# Engineering / Spectral view

## Spectral display

The editor's second way of showing sound: a **spectrogram** of the whole file, time across as in
the waveform, frequency up the side, and level as colour, so that every note, harmonic, hiss, hum,
click and breath can be seen where it is. View > Spectral Display (Shift+D) turns it on;
**Blend Towards Spectrogram** and **Blend Towards Waveform** (Alt+Shift+Up and Down) mix the two
pictures a tenth at a time, so the shape of the waveform stays visible over the frequencies. It is
also where spectral editing happens: the Repair menu's box, lasso and brush select in time and
frequency at once (see [Spectral editing](research:Spectral%20editing)).

PHI's analysis follows the spectrogram layer of Sonic Visualiser, with the settings described in
the entries below.

The sound spectrograph was built at Bell Telephone Laboratories during the Second World War to
study speech, and published in 1946; *Visible Speech* (1947) taught people to read words from its
pictures. Digital spectrograms became the editing surface of restoration with CEDAR Retouch and
iZotope RX in the 2000s, and of Adobe Audition's spectral frequency display.

- [Spectrogram](https://en.wikipedia.org/wiki/Spectrogram), Wikipedia.
- W. Koenig, H. K. Dunn and L. Y. Lacy, "The Sound Spectrograph", *Journal of the Acoustical
  Society of America* 18 (1946).
- Ralph K. Potter, George A. Kopp and Harriet C. Green, *Visible Speech* (Van Nostrand, 1947).
- Chris Cannam, Christian Landone and Mark Sandler, "Sonic Visualiser: An Open Source
  Application for Viewing, Analysing, and Annotating Music Audio Files", ACM Multimedia (2010).

## Short-time Fourier transform

The analysis behind every spectrogram: the sound is cut into short overlapping frames, each is
faded in and out by a [window](research:Window%20functions) and transformed with the
[FFT](research:The%20Fourier%20transform%20and%20the%20FFT), and the spectra are laid side by side.
The **short-time Fourier transform** (STFT) can also be undone: the frames, changed or not, are
transformed back and overlap-added into sound again, which is how PHI's noise reduction, DeEss,
Auto Heal and Spectral Gain change only the frequencies they touch.

Its fundamental limit is the **time-frequency trade-off**: a frame long enough to tell close
frequencies apart is too long to place a short sound exactly in time, and the reverse. Dennis
Gabor described it in 1946 as the acoustic counterpart of the uncertainty principle of quantum
physics.

- [Short-time Fourier transform](https://en.wikipedia.org/wiki/Short-time_Fourier_transform),
  Wikipedia.
- Dennis Gabor, "Theory of Communication", *Journal of the Institution of Electrical Engineers*
  93 (1946).
- Jont B. Allen, "Short Time Spectral Analysis, Synthesis, and Modification by Discrete Fourier
  Transform", *IEEE Transactions on Acoustics, Speech, and Signal Processing* 25 (1977).

## Window size, shape, overlap and oversampling

The **Analysis** settings of View > Spectrogram Settings:

- **Window size**, from 256 to 32 768 samples: larger windows separate frequencies more finely
  (each bin is the sample rate divided by the size apart), smaller ones place sounds more exactly
  in time. 2048 at 48 kHz gives bins 23 Hz apart and frames 43 ms long, a balance for music;
  speech and drums read better smaller, bass notes and hum larger.
- **Window shape**: Hann, the usual choice, Hamming, or Blackman-Harris, which keeps quiet sounds
  beside loud ones visible.
- **Overlap**, from none to 93.75 %: how much each frame overlaps the next when the view is zoomed
  out. More overlap misses fewer short sounds between frames; 75 % is the usual value.
- **Oversampling**, 1× to 8×: zero padding, which draws the spectrum more smoothly between
  frequencies without adding detail (see
  [FFT size, resolution and zero padding](research:FFT%20size,%20resolution%20and%20zero%20padding)).

- Julius O. Smith III, [*Spectral Audio Signal Processing*](https://ccrma.stanford.edu/~jos/sasp/)
  (W3K Publishing, 2011).
- [Short-time Fourier transform](https://en.wikipedia.org/wiki/Short-time_Fourier_transform),
  Wikipedia.
- [Window function](https://en.wikipedia.org/wiki/Window_function), Wikipedia.
- [Zero padding](https://en.wikipedia.org/wiki/Discrete_Fourier_transform#Zero_padding), Wikipedia.

## Frequency scales: linear, log and mel

How the frequencies are spread up the display. **Linear** gives every hertz the same height, so
the top octave, 10 to 20 kHz, takes half the picture: useful for high-frequency detail, noise and
the products of aliasing. **Log** gives every octave the same height, as a piano keyboard and the
ear do: the usual choice for music. **Mel** follows how people judge pitch distance, close to linear
below about 1 kHz and logarithmic above: the scale of speech analysis and of most machine
listening.

Alt+wheel over the spectrogram zooms the frequencies around the mouse, and **Frequencies Shown**
sets the range directly (Full Range, Alt+0, shows everything); the log scale cannot reach 0 Hz and
starts at 10 Hz or above.

The mel scale was published by Stanley Smith Stevens, John Volkmann and Edwin Newman in 1937 from
listeners' judgements of pitches "half as high"; its name is from melody.

- [Mel scale](https://en.wikipedia.org/wiki/Mel_scale), Wikipedia.
- S. S. Stevens, J. Volkmann and E. B. Newman, "A Scale for the Measurement of the Psychological
  Magnitude Pitch", *Journal of the Acoustical Society of America* 8 (1937).

## Peak bins and time-frequency reassignment

The **Bins** setting chooses what is drawn of each frame's spectrum. **All Bins** draws every bin, the
ordinary spectrogram. **Peak Bins** draws only bins that are louder than their neighbours, so the
partials of tones stand out from the noise. **Peak Frequencies** draws only the peaks and moves each
to its exact frequency, worked out from how fast its phase turns between the window and its time
derivative: **time-frequency reassignment**. It gives sharp lines for tones and harmonics, far finer
than the bins, which makes pitch, vibrato and tuning readable.

Kunihiko Kodera, Roger Gendrin and Claude de Villedary proposed reassignment for geophysical signals
in 1976; François Auger and Patrick Flandrin generalised it in 1995, and Kelly Fitz and Lippold
Haken brought it to sound analysis.

- [Reassignment method](https://en.wikipedia.org/wiki/Reassignment_method), Wikipedia.
- François Auger and Patrick Flandrin, "Improving the Readability of Time-Frequency and
  Time-Scale Representations by the Reassignment Method", *IEEE Transactions on Signal
  Processing* 43 (1995).

## Gain, range, normalising and colours

The **Display** settings that decide how levels become colours. **Gain** adds to every level, to
bring up a quiet recording. **Range** is how far below the top a level is still drawn: everything
quieter is the bottom colour, so a smaller range hides background noise and a larger one shows it.
**Normalise** makes the loudest frequency of each moment full colour (Each Column), to see quiet
passages, or the loudest of the visible area (Visible Area). The **Colours** are the theme's or a
map: Sunset (in the manner of magma), Ice, Green, Grey, Grey on White, Rainbow and Cherry.

Colour maps whose brightness rises evenly, as Sunset and the greys do, are easier to read than a
rainbow, whose bands of yellow and cyan look like edges that are not in the sound; the
perceptually uniform maps of matplotlib (2015) made the point widely known.

- [Spectrogram](https://en.wikipedia.org/wiki/Spectrogram), Wikipedia.
- Kenneth Moreland, "Why We Use Bad Color Maps and What You Can Do About It", *Electronic
  Imaging* (2016).

## Pitch line

The pitch of the main voice or instrument traced over the spectrogram (View > Pitch Line,
Shift+P), worked out in the background once per take. It is what Repair > Correct Pitch works
from. PHI finds the pitch with the **YIN** algorithm on the channels mixed to mono: for each frame
it measures how different the sound is from itself delayed by each possible period, takes the
first delay that makes it nearly repeat, and refines it between samples; octave slips are corrected
and stray frames dropped afterwards. Where there is no clear pitch (silence, noise, a dense chord)
the line breaks.

Alain de Cheveigné and Hideki Kawahara published YIN in 2002, named after the yin and yang of
autocorrelation and cancellation it balances; it remains a standard of monophonic pitch tracking.

- [Pitch detection algorithm](https://en.wikipedia.org/wiki/Pitch_detection_algorithm),
  Wikipedia.
- Alain de Cheveigné and Hideki Kawahara, "YIN, a Fundamental Frequency Estimator for Speech and
  Music", *Journal of the Acoustical Society of America* 111 (2002).
