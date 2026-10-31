<!--
Research > Engineering > Restoration: the role of restoration in audio engineering, music
production and archives, and the terms of the Repair menu (noise reduction, DeClick, DeClip,
DeHum, DeEss, spectral editing, Auto Heal, Find Similar Sounds, Diagnostics, Correct Pitch).
-->

# Engineering / Restoration

## Audio restoration

Removing what does not belong in a recording, or rebuilding what is missing from it, while
keeping everything that does belong untouched: hiss, hum, clicks, crackle, clipping, dropouts,
coughs, phone rings, harsh sibilance. The work has two halves. **Detection** finds the damage:
a click is a few samples that the audio around them does not predict, hum is a steady tone at the
mains frequency and its harmonics, clipping is a run of samples stuck at one level. **Repair**
replaces the damage with a plausible estimate, made from the sound just before and after it,
from the same moment at other frequencies, or from a learned picture of the noise.

The rule of the craft is that a repair should be inaudible, and that it is better to leave a
little noise than to take away music with it: the artefacts of over-processing (a dull,
underwater sound, "musical noise", smeared transients) are worse than what they replaced. PHI's
Repair menu holds the restoration tools; its Diagnostics panel finds problems without changing
anything, to review them first.

Digital restoration began in the 1970s: Thomas Stockham at the University of Utah used blind
deconvolution in 1975 to take the horn's resonances out of Enrico Caruso's acoustic recordings of
1907 to 1920, released by RCA on the Soundstream system. Peter Rayner's group at Cambridge
University developed statistical methods for the British Library's National Sound Archive, which
led to the founding of CEDAR Audio in 1988; Simon Godsill and Rayner's book of 1998 set out the
theory. Spectral editors (CEDAR Retouch, iZotope RX, Adobe Audition, SpectraLayers) made it visual
in the 2000s.

- [Audio restoration](https://en.wikipedia.org/wiki/Audio_restoration), Wikipedia.
- [Noise reduction](https://en.wikipedia.org/wiki/Noise_reduction), Wikipedia.
- Simon J. Godsill and Peter J. W. Rayner, *Digital Audio Restoration: A Statistical Model Based
  Approach* (Springer, 1998).
- Thomas G. Stockham, Thomas M. Cannon and Robert B. Ingebretsen, "Blind Deconvolution through
  Digital Signal Processing", *Proceedings of the IEEE* 63 (1975).

## Restoration in audio engineering

In everyday engineering restoration is less about old records than about saving the recording
that was made today. Location sound, interviews, podcasts and live recordings are made where
nobody controls the noise: air conditioning, traffic, a fridge, a hum loop in the cables, a
microphone too close to the mouth, a level that clipped in the loudest moment. Post-production
for film, television and broadcast cleans dialogue as routinely as it edits it, because the take
with the right performance is rarely the cleanest one; a guest's phone ringing through the best
answer of an interview is a job for Auto Heal, not a reason to lose the answer.

Restoration also comes before measurement and mastering: a DC offset, a click or a burst of
clipping throws off loudness and peak readings, and the Diagnostics panel is the quality check
before delivery that broadcasters and distributors run on arrival.

- [Post-production](https://en.wikipedia.org/wiki/Post-production), Wikipedia.
- [Production sound mixer](https://en.wikipedia.org/wiki/Production_sound_mixer), Wikipedia.
- John Purcell, *Dialogue Editing for Motion Pictures* (Focal Press, 2nd ed. 2013).

## Restoration in music production

In music the same tools serve the music's own sound. They clean the noise of a guitar amplifier
between phrases, the hum of a vintage keyboard, the clicks of a bad edit or a buffer underrun, the
squeak of fingers on strings, a chair creak in a quiet classical take; they tame sibilance on a
vocal before compression makes it worse (DeEss); they rescue a performance that clipped the
converter (DeClip). Spectral editing removes a single cough from an orchestral recording or a
bleed of the click track from a vocal microphone. Pitch correction (Correct Pitch) belongs to the
same family: repairing one note rather than replacing the take.

Restoration is also a creative source: samples from old records are often cleaned (or kept dirty
on purpose), and the noise profile of a vinyl crackle or tape hiss can itself become a texture.
Producers judge every repair by ear against the original, since what counts as damage depends on
the music: the breath of a singer or the rattle of a snare is part of the performance.

- [Audio editing software](https://en.wikipedia.org/wiki/Audio_editing_software), Wikipedia.
- [Record producer](https://en.wikipedia.org/wiki/Record_producer), Wikipedia.
- [Sampling (music)](https://en.wikipedia.org/wiki/Sampling_(music)), Wikipedia.

## Restoration of archival recordings

Archives hold more than a century of sound on carriers that decay: wax cylinders (from 1877),
shellac and lacquer discs, wire, magnetic tape whose binder turns sticky, cassettes, early digital
formats. Their work has three stages. **Transfer** plays the carrier as well as it can be played,
with the right stylus, speed and equalisation curve, into a high-resolution digital file (at least
48 kHz and 24 bits by the archival guidelines). **Preservation** keeps that file, unaltered, as the
archival master. **Restoration** then makes access copies, for listening, broadcast or reissue,
from which clicks, crackle, hiss and hum are removed.

The ethics of the field, set out by the International Association of Sound and Audiovisual
Archives (IASA), keep restoration separate from preservation: the original transfer is never
replaced by a restored version, every process is documented, and nothing is added that was not
in the recording, so that later listeners, and better tools, can always go back to the source.
Restoration tools that only add back their change, and leave everything outside the repair
bit-for-bit the same, as PHI's spectral tools do, suit that rule well.

Restored archive recordings are how most people now hear early jazz, blues, opera, folk field
recordings, speeches and oral history; reissue labels and national libraries from the British
Library to the Library of Congress run restoration studios for them.

- [Sound recording and reproduction](https://en.wikipedia.org/wiki/Sound_recording_and_reproduction),
  Wikipedia.
- [Preservation of magnetic audiotape](https://en.wikipedia.org/wiki/Preservation_of_magnetic_audiotape),
  Wikipedia.
- [Phonograph cylinder](https://en.wikipedia.org/wiki/Phonograph_cylinder), Wikipedia.
- [Sound archive](https://en.wikipedia.org/wiki/Sound_archive), Wikipedia.
- IASA Technical Committee, *TC-03: The Safeguarding of the Audio Heritage: Ethics, Principles
  and Preservation Strategy* (IASA, 2005).
- IASA Technical Committee, *TC-04: Guidelines on the Production and Preservation of Digital
  Audio Objects* (IASA, 2nd ed. 2009).

## Noise profile and noise reduction

Turning down steady background noise (hiss, air conditioning, room tone, a hum that has too many
harmonics for notches) while keeping the sound above it. First **Learn Noise Profile** (Ctrl+Alt+N)
on a stretch that holds only the noise: PHI averages its power spectrum in 2048-point frames, per
channel. Then **Noise Reduction** works through the audio in short-time Fourier frames and decides,
bin by bin, how much of each frequency to keep: nothing of what is below the noise plus the
**sensitivity** (in dB), all of what is 6 dB beyond that, and a soft slope between. What is not kept
is turned down by the **reduction** amount rather than removed, and after a sound the reduction
comes back over the **release** time.

Single bins of noise that pass the threshold at random, frame after frame, make a twittering
artefact called **musical noise**; PHI averages each bin's gain with its neighbours and lets gains
fall back slowly to avoid it. Too much reduction makes the sound dull and watery: a few dB less
than "all of it" usually sounds better.

The method is a spectral gate, a refinement of the **spectral subtraction** that Steven Boll
published in 1979; Yariv Ephraim and David Malah's statistical estimator (1984) is the basis of
many commercial denoisers.

- [Noise reduction](https://en.wikipedia.org/wiki/Noise_reduction), Wikipedia.
- Steven F. Boll, "Suppression of Acoustic Noise in Speech Using Spectral Subtraction", *IEEE
  Transactions on Acoustics, Speech, and Signal Processing* 27 (1979).
- Yariv Ephraim and David Malah, "Speech Enhancement Using a Minimum Mean-Square Error Short-Time
  Spectral Amplitude Estimator", *IEEE Transactions on Acoustics, Speech, and Signal Processing*
  32 (1984).

## Clicks, crackle and DeClick

A click is a short burst, a few samples to a few milliseconds, that has nothing to do with the
music: a scratch or dust on a record, a bad edit, a digital glitch. Crackle is many small ones.
Repair > DeClick finds them with a **linear prediction** model: from the samples before it, the
model predicts each sample; music is largely predictable and a click is not, so a click shows up
as a burst of prediction error. The threshold is the **sensitivity**, in robust standard deviations
of the error (the median absolute value times 1.4826, which a few clicks cannot inflate); lower
values find more.

Each click is then rebuilt by **least-squares autoregressive interpolation**: the missing samples
are chosen so that the model's prediction error, before, through and after the gap, is as small as
possible, which continues the waveform's frequencies through the gap. Clicks too long to rebuild
that way are counted and left for Auto Heal.

The model is the autoregressive one of speech coding, fitted with the Levinson-Durbin recursion
(Norman Levinson 1947, James Durbin 1960); A. J. E. M. Janssen, Raymond Veldhuis and Lodewijk
Vries published the interpolation in 1986 at Philips Research, for scratched compact discs and
records.

- [Linear prediction](https://en.wikipedia.org/wiki/Linear_prediction), Wikipedia.
- [Autoregressive model](https://en.wikipedia.org/wiki/Autoregressive_model), Wikipedia.
- [Levinson recursion](https://en.wikipedia.org/wiki/Levinson_recursion), Wikipedia.
- [Median absolute deviation](https://en.wikipedia.org/wiki/Median_absolute_deviation),
  Wikipedia.
- A. J. E. M. Janssen, R. N. J. Veldhuis and L. B. Vries, "Adaptive Interpolation of
  Discrete-Time Signals That Can Be Modeled as Autoregressive Processes", *IEEE Transactions on
  Acoustics, Speech, and Signal Processing* 34 (1986).

## Clipping and DeClip

When a recording clipped, the tops of the waveform are flat: two or more samples in a row stuck at
the same extreme level. Repair > DeClip finds such runs at or above a **level**, a percentage of
the loudest sample in the range, and rebuilds the missing tops from an autoregressive model of the
audio around them, as DeClick rebuilds gaps, so the curve rises above the old ceiling as it would
have. The result can peak above 0 dBFS; PHI works in floating point, so nothing is lost, but the
level must come down (Gain or Normalize) before the file is saved in a fixed-point format.

Declipping cannot recover what was never recorded beyond a few samples; long clipped stretches
are counted and left.

- [Clipping (audio)](https://en.wikipedia.org/wiki/Clipping_(audio)), Wikipedia.
- [Autoregressive model](https://en.wikipedia.org/wiki/Autoregressive_model), Wikipedia.

## Hum and DeHum

Mains hum is the frequency of the electricity supply, 50 Hz in most of the world and 60 Hz in the
Americas and parts of Asia, leaking into audio through ground loops, unshielded cables, lighting
dimmers and transformers, with a series of harmonics above it (a buzz has many). Repair > DeHum
finds the hum by itself when its frequency is left at 0: it measures the level at 50 and 60 Hz
and their next harmonics with the Goertzel algorithm, takes the one that stands clear of its
surroundings, and refines it to 0.01 Hz, because supplies drift. It then removes a narrow notch at
the hum and each harmonic, up to the number chosen, each a set **width** in Hz wide; the filters are
settled on the audio before the range so that they start already cancelling.

- [Mains hum](https://en.wikipedia.org/wiki/Mains_hum), Wikipedia.
- [Ground loop (electricity)](https://en.wikipedia.org/wiki/Ground_loop_(electricity)),
  Wikipedia.
- [Goertzel algorithm](https://en.wikipedia.org/wiki/Goertzel_algorithm), Wikipedia.
- [Band-stop filter](https://en.wikipedia.org/wiki/Band-stop_filter), Wikipedia.

## Sibilance and DeEss

Sibilance is the hiss of "s", "sh", "z" and "ch", from about 4 to 10 kHz: natural in speech and
singing, but close microphones, bright microphones, compression and loudness processing can make
it piercing. Repair > DeEss watches the level of the band from a frequency up (5 kHz by default,
with a soft lower edge a sixth of an octave wide) in frames of about 20 ms, quick enough to follow
each sibilant; when it rises above the **threshold**, that band alone is turned down by the excess,
up to the **maximum reduction**, easing off at 100 dB a second.

The de-esser began in radio and record cutting in the 1960s, where sibilance distorted
transmitters and lathes; it is a compressor that listens to, and works on, the highs alone.

- [De-essing](https://en.wikipedia.org/wiki/De-essing), Wikipedia.
- [Sibilant](https://en.wikipedia.org/wiki/Sibilant), Wikipedia.

## Spectral editing

Editing in time and frequency at once, on the [spectral display](research:Spectral%20display):
selecting not just a stretch of time but the region of the spectrogram that the problem occupies,
so that a cough, a squeak or a bird can be taken out of the middle of the music without touching
what sounds at the same time at other frequencies. The Repair menu's selection tools are the
**Box Selection Tool** (a rectangle of time and frequency; Frequency Range sets the band by number
and All Frequencies clears it), the **Lasso Tool** (any shape), and the **Brush Tool** (painted, with
Smaller and Larger Brush); **Add Harmonics** extends a selection to the overtones of what it holds,
at two, three and more times its frequencies, since a sound's harmonics are part of it.

**Spectral Gain** turns the selected region up or down by a number of dB, removing it at −120 dB.
Like every spectral tool in PHI it works on a short-time Fourier transform of the audio around the
selection and adds back only the change, so everything outside the selection and its feathered
edge stays bit-for-bit the same.

- [Spectrogram](https://en.wikipedia.org/wiki/Spectrogram), Wikipedia.
- [Short-time Fourier transform](https://en.wikipedia.org/wiki/Short-time_Fourier_transform),
  Wikipedia.
- [Audio editing software](https://en.wikipedia.org/wiki/Audio_editing_software), Wikipedia.

## Auto Heal and the Spot Healing Brush

Rebuilding a selection from the sound around it, for clicks too long for DeClick, coughs, squeaks,
chair creaks and phone rings, up to four seconds at a time. Repair > Auto Heal (Ctrl+U) heals the
selection; the **Spot Healing Brush** heals what you paint over at once. PHI analyses the frames
just before and just after the damage and learns, for each frequency on each side, its typical
level (ignoring stray peaks such as the click itself), its phase and how fast it turns, and how
steady that turning is: near 1 for a held tone, near 0 for noise. Across the gap it fades from the
sound before to the sound after: steady tones are continued with their phase, so a held note goes
on through the gap, and noise is rebuilt as fresh noise of the same level.

Short clicks get short analysis windows so the repair stays tight; a band selection gets finer
frequencies. The name and the brush come from photo editing, where Photoshop's healing brush
(2002) fills a blemish from its surroundings.

- [Inpainting](https://en.wikipedia.org/wiki/Inpainting), Wikipedia.
- [Phase vocoder](https://en.wikipedia.org/wiki/Phase_vocoder), Wikipedia.
- Amir Adler and others, "Audio Inpainting", *IEEE Transactions on Audio, Speech, and Language
  Processing* 20 (2012).

## Find Similar Sounds

Repair > Find Similar Sounds takes the selection as an example (a click, a cough, a page turn, a
squeak of the same chair) and finds the other places in the file whose spectrum stands out from its
surroundings in the same way, each as a region of the example's length and band, ready to repair
together. **Strictness** (0 to 1) decides how close a match must be. It turns a hundred repairs of
the same noise into one.

Matching a template against a signal is one of the oldest methods of detection, the matched filter
of radar.

- [Template matching](https://en.wikipedia.org/wiki/Template_matching), Wikipedia.
- [Matched filter](https://en.wikipedia.org/wiki/Matched_filter), Wikipedia.

## Diagnostics: clicks, clipping, dropouts and silence

The Diagnostics panel (Repair > Diagnostics Panel, Ctrl+Shift+D), as Audition's, scans the
selection or the whole file and lists problems without changing anything, so they can be heard and
judged first, then repaired one by one, all at once, or marked:

- **Clicks**: found as DeClick finds them, each with how many times louder it is than the audio
  around it.
- **Clipping**: runs of samples stuck at the clip level.
- **Dropouts**: stretches of digital silence of at least 1 ms with sound on both sides, the mark of
  lost samples from a buffer underrun, a bad disk or a failed transfer.
- **Silence**: stretches quieter than a threshold (RMS over 10 ms) for at least a minimum time,
  for trimming or for finding gaps.

Clicks, clipping and dropouts can be rebuilt from the list; silence is only listed.

- [Audio restoration](https://en.wikipedia.org/wiki/Audio_restoration), Wikipedia.
- [Buffer underrun](https://en.wikipedia.org/wiki/Buffer_underrun), Wikipedia.
- [Quality control](https://en.wikipedia.org/wiki/Quality_control), Wikipedia.

## Pitch correction

Repair > Correct Pitch moves a selected note to a target pitch, by default the nearest note of the
scale, and can flatten its drift and vibrato towards it. The pitch comes from the
[pitch line](research:Pitch%20line) point by point; the note's own pitch is the median of its voiced
points; Rubber Band (its R3 engine, with formants kept so a voice does not turn into a chipmunk)
then shifts the audio by a pitch that changes smoothly over time, easing in and out at the edges so
the audio around the selection is untouched.

Automatic pitch correction reached the studio with Antares Auto-Tune in 1997, by Andy Hildebrand,
who had worked on seismic data analysis; note-by-note editing followed with Celemony's Melodyne
(2001).

- [Pitch correction](https://en.wikipedia.org/wiki/Pitch_correction), Wikipedia.
- [Auto-Tune](https://en.wikipedia.org/wiki/Auto-Tune), Wikipedia.
- [Formant](https://en.wikipedia.org/wiki/Formant), Wikipedia.
- [Rubber Band Library](https://breakfastquay.com/rubberband/), Breakfast Quay.
