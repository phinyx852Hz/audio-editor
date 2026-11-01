# The Visualizer and Sonify

[]{.ix t="Visualizer"}The Visualizer draws what PHI plays, after the Master Section: fourteen views, from scientific analyzers to light shows, plus the whole MilkDrop library. It can float in a window, fill the screen, or glow behind the whole PHI window. Sonify runs the other way, and turns pictures into music.

## Showing it

| Keys | Does |
|---|---|
| `Shift+F11` | The Visualizer in its own floating window, with a view picker |
| `F11` | Full screen; `Esc` goes back |
| `Ctrl+F11` | Behind the whole PHI window, as its background |
| `Alt+F11` / `Alt+Shift+F11` | Next and previous view (inside the window, `Left` and `Right` too) |
| `Ctrl+Shift+F11` | Choose a view from a list |

The Visualizer menu (`Alt+Z`) has the views in **Artistic**, **Science**, **MilkDrop** and **Digital Rain** submenus. In the Find panel, Visualizations lists them all; moving through the list shows each in the open Visualizer.

[]{.ix t="Visualizer!behind PHI"}**Behind PHI.** With the Visualizer behind the window (`Ctrl+F11`), the window's opacity (`Ctrl+Alt+-` and `Ctrl+Alt+=`, or the Opacity slider) sets how much of it shows through, from almost all Visualizer to almost all PHI. Every panel, list and field goes see-through together.

![The Visualizer behind PHI, glowing through the panels.](images/visualizer-behind.png)

### What a moving background costs {#background-cpu}

[]{.ix t="CPU use!backgrounds"}[]{.ix t="Visualizer!CPU use"}A background that moves costs processor time for as long as it is shown, because it is drawn again many times a second. The see-through PHI window over it has to be redrawn with it. A still picture, such as a [wallpaper](#wallpaper), costs almost nothing once it has been drawn, because nothing in it changes.

| Behind PHI | What it costs |
|---|---|
| Nothing (the plain theme) | Nothing extra |
| A wallpaper (Options > Wallpaper) | Almost nothing after it is first drawn |
| The Visualizer (`Ctrl+F11`), MilkDrop most of all | Drawing every frame while sound plays; after two seconds of silence it slows to about ten frames a second |
| A web page playing video, or an animated site | Decoding and drawing the video, as a browser tab does |

::: important
If PHI feels slow, the CPU meter (View > CPU Meter) climbs, or you hear clicks and dropouts while recording or mixing a large project, turn the moving background off first, or use a wallpaper instead: `Ctrl+F11` again for the Visualizer, or untick Visualizer > Web Page > Web Page Behind PHI for a web page. On a laptop, a moving background also runs the battery down faster. A still background, or none, leaves the most power for the audio.
:::

## The views

| Artistic | | Science | |
|---|---|---|---|
| Bars | cava-style, with falling peaks | Spectrum Analyzer | FFT 4096, log frequency, dBFS grid, peak hold, the strongest peak with its note |
| Radial | | Third-Octave Bands | 31 IEC bands with peak hold |
| Circle Wave | | Oscilloscope | 40 ms, triggered |
| Starfield | | Spectrogram | Scrolling, log frequency |
| Ribbons | | Waterfall | |
| Digital Rain | | 3D Spectrum | Frequency, time and level as a coloured surface, about 4 s deep |
| Spectral Landscape | The 3D surface as glowing mountains | | |
| Picture | A picture, video or camera, for Sonify | | |

In the 3D views, drag to turn the camera, use the wheel to zoom, and press `Home` to reset it.

![The 3D Spectrum.](images/visualizer-3d.png)

### Digital Rain

[]{.ix t="Digital Rain"}The green falling symbols of The Matrix, written for PHI. They fall at a tempo from 0 BPM (still) to 240 BPM, a row per sixteenth note, and glow with the music. In its window, `Up` and `Down` change the tempo (`Shift`: by 1), tap `T` to set it, and `0` stops it. View > Digital Rain Tempo sets it from anywhere, and **Digital Rain Follows the File's Tempo** uses the file's BPM tag.

### MilkDrop

[]{.ix t="MilkDrop"}[]{.ix t="projectM"}The classic Winamp-era MilkDrop presets, played by projectM. Get the free *Cream of the Crop* collection, 9,795 presets in folders such as Fractal, Geometric, Hypnotic, Particles, Reaction, Supernova and Waveform, with View > Download MilkDrop Presets, or put your own `.milk` files in `~/.local/share/phi/milkdrop`.

Presets change by themselves every 30 seconds. `Up` and `Down` (or `Alt+F12` and `Alt+Shift+F12` from anywhere) go to the next or previous one, `L` keeps the current one, and `Ctrl+Shift+F12` opens a searchable list that shows each preset as you move through it.

![A MilkDrop preset.](images/milkdrop.png)

## A web page behind PHI {#web-page}

[]{.ix t="Web Page behind PHI"}Visualizer > Web Page puts any web page (a video, a live feed, a generative art site) behind the whole PHI window, like the Visualizer behind it, and Performance > Record Video records it with PHI.

- **Web Page Address** sets the page.
- **Use the Web Page** brings it to the front to click, log in or start a video; `Esc` or **Back to PHI** puts it behind again.
- Its sound is off unless **Web Page Sound** is ticked; it then plays through the desktop, and Performance can record it as an application.

Logins are kept in PHI's own browser profile. This needs Qt WebEngine when PHI is built.

## Sonify {#sonify}

[]{.ix t="Sonify"}Visualizer > Sonify (or the Sonify button in the Visualizer window) plays pictures as notes, still or moving. The source is whatever the Visualizer shows (any view, MilkDrop too), a picture, a video file or a camera shown in the Picture view, or the web page behind PHI.

![Sonify with a picture of a sunflower: Spiral mode, the Φ scale and the Φ rhythm.](images/sonify.png)

**How it hears a picture.** The picture is read small, 64 by 48, about 20 times a second. Higher up is a higher note, brighter is louder, and change from one column or frame to the next strikes General MIDI drums on channel 10: kick at the bottom, snare in the middle, hi-hat at the top. With the **Φ rhythm** button, the drums play only on the 1s of the Fibonacci word (1011010110110…), a groove that never quite repeats.

**Modes.**

| Mode | Plays | For |
|---|---|---|
| Scan | A line sweeps across, a column per sixteenth note, playing the bright spots it crosses | Still pictures |
| Lanes | Each voice has a lane, playing where its light is and gliding as it moves | Moving pictures |
| Spiral | The next point of a sunflower spiral each step, turning by the golden angle from the centre outwards, so a still picture plays as a melody that never repeats | Still pictures |

[]{.ix t="Φ scale"}**Pitch.** The Pitch slider goes from *free* (the picture's own pitch, between the notes, by pitch bend) to the notes of a scale: Chromatic, Major, Minor, Pentatonic, Minor Pentatonic, Blues, Dorian, Whole Tone, or **Φ**, the powers of the golden ratio folded into the octave (see [Between the keys](#phi-frequency)), in any key. Each voice has a MIDI channel of its own (1 to 8), as in MPE, so every note bends on its own.

Tempo, voices, steps across, the note range and how bright a spot must be to sound are set in the bar.

**What it plays.** The built-in Synth (put into a free patchbay slot), or whatever the MIDI keyboard plays: Simplest, the Slicer, the mixer's armed track, or the Wavetable, FM and other synths. Set **Bend** to the instrument's pitch-bend range so free pitches land where they should.
