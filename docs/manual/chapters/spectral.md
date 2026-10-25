# The Spectral View

[]{.ix t="Spectral view"}[]{.ix t="Spectrogram"}The waveform shows how loud a sound is over time. The spectral view shows what it is made of: time runs across, frequency runs up, and brightness is level. A hum is a steady line, a voice a stack of moving lines, a click a thin vertical stroke, a cymbal a bright cloud at the top. Because each sound has its own place, you can select it and repair it without touching what is around it (see [Repair and Restoration](#repair)).

![The spectral view of a voice: the harmonics as a stack of lines, a hum at the bottom, and three clicks as vertical strokes.](images/spectral-view.png)

## Showing it

| Keys | Does |
|---|---|
| `Shift+D` | Switches between the waveform and the spectrogram |
| `Alt+Shift+Up` | Blends a tenth towards the spectrogram |
| `Alt+Shift+Down` | Blends a tenth towards the waveform |
| `Alt+Shift+D` | Spectrogram Settings |

With a blend, both show at once, the spectrogram glowing through the waveform. PHI remembers the blend. Every channel has its own spectrogram, and all of it stays fully editable.

## Zooming in frequency

[]{.ix t="Zoom!frequency"}`Alt` with the wheel zooms the frequencies around the mouse; `Alt+=` and `Alt+-` zoom in and out and `Alt+0` shows them all. Frequencies are labelled at the left. As you move the mouse, the readout shows the time, frequency, note and level under it.

## Spectrogram Settings {#spectrogram-settings}

[]{.ix t="Spectrogram Settings"}View > Spectrogram Settings (`Alt+Shift+D`) offers Sonic Visualiser's controls. Changes show at once and are remembered.

![Spectrogram Settings.](images/spectrogram-settings.png)

| Setting | Choices | Why |
|---|---|---|
| Window size | 256 to 32768 | Larger shows pitch more finely, smaller shows timing more sharply |
| Window shape | Hann, Hamming, Blackman-Harris | Blackman-Harris keeps loud tones from smearing over quiet ones |
| Overlap and oversampling | | Smoother pictures, at more work for the processor |
| Scale | Linear, log or mel | Log and mel give the low end the room the ear gives it |
| Bins | All Bins, Peak Bins, Peak Frequencies | Peak Frequencies draws sharp lines at each peak's exact frequency, found by time-frequency reassignment |
| Colour map, gain, range | | How level turns into colour |
| Normalise | Per column or over the view | Per column brings out quiet passages |

## Pitch Line and Correct Pitch {#pitch}

[]{.ix t="Pitch Line"}[]{.ix t="Correct Pitch"}**Pitch Line** (`Shift+P`, View menu) traces the main voice's or instrument's pitch over the spectrogram, and the mouse readout adds the pitch and note under the mouse.

**Correct Pitch** (`Ctrl+Alt+Shift+P`, Repair menu) moves a selected note to the nearest note, or to one you choose, and can flatten its drift and vibrato. It keeps the voice's character, using Rubber Band.

## The time-and-frequency selection {#spectral-selection}

[]{.ix t="Selection!spectral"}In the spectral view, a drag selects a box of time and frequency instead of a stretch of time. Drag over nearly the whole height for every frequency. To type the band instead, use Repair > Frequency Range (`Ctrl+Alt+F`); All Frequencies (`Ctrl+Alt+Shift+F`) returns to a selection of time only. The drawing tools for other shapes are in [Repair and Restoration](#repair).
