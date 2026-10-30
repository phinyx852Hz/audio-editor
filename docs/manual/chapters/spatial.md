# Surround, Binaural and Ambisonics

The Spatial menu (`Alt+I`) places sound around the listener: in surround, on headphones, or in a full sphere. This chapter covers channel layouts and surround files, binaural placement, ambisonics, your own speaker arrays and the Ambisonic Studio. To place a whole mix of tracks in 3D, and to put surround and ambisonic files on the mixer's tracks, see [Spatial mixing](#spatial-mixing) in the mixer.

## Surround files

[]{.ix t="Surround"}PHI edits files of any channel count, from mono to 5.1, 7.1 and 7.1.4. Each lane and meter is labelled with its speaker, loudness weights the surrounds as ITU-R BS.1770 asks, and the Master Section, renders and saves keep every channel. Surround WAVs carry their speaker layout.

![A 7.1 file: a lane per speaker, each labelled.](images/surround-7-1.png)

**Listening.** Surround files are heard folded down to stereo, with the centre and surrounds at −3 dB and the LFE left out, which suits headphones. Options > Play Surround on Separate Outputs sends each channel to its own output instead, when the device has enough.

A plugin that only handles stereo processes the front left and right, and its card says "L/R only".

## Channel layouts {#channel-layout}

[]{.ix t="Channel Layout"}Some channel counts fit more than one layout. Spatial > Channel Layout (`Ctrl+Alt+Shift+L`) says which a file is:

| Channels | Choices |
|---|---|
| 4 | Quad, or first-order Ambisonics |
| 6 | 5.1, or a quad-style 6 |
| 7 | 6.1, or 5.2 (two subwoofers, LFE1 and LFE2) |
| 9 | 7.1.2, or 7.2 |
| 13 | 7.1.4-style 13, as 7.2.4 |
| 16 | 9.1.6, or third-order Ambisonics |

Channel names, meters and the stereo fold-down follow it.

::: note
9.2.6 (17 channels) and fourth-order Ambisonics and higher need more than the 16 channels the engine plays today. The Analysis Meters window still measures them, up to tenth order.
:::

## Binaural {#binaural}

[]{.ix t="Binaural"}[]{.ix t="HRTF"}Spatial > Binaural (`Ctrl+Alt+Shift+B`, or type "binaural" in Find) places a mono or stereo sound around your head, for headphones. The result is always stereo.

![Binaural: the head from above and from the side, with the sound's place.](images/binaural.png)

Drag the sound around the head seen from above, and up or down in the side view, or use the keys:

| Keys | Moves the sound |
|---|---|
| `Left` / `Right` | Round the head |
| `Up` / `Down` | Closer or further |
| `Page Up` / `Page Down` | Higher or lower |
| `Shift+Left` / `Shift+Right` | Narrower or wider (for stereo) |

Settings: azimuth, elevation, width, distance, room amount and decay, output level, and the **HRTF set**, the measurements of a head and ears the placement is heard through. The MIT KEMAR set comes built in; put your own `.sofa` files in `~/.local/share/phi/hrtf`.

**Preview** plays the result in a loop and follows your changes. A selection's tail rings on after it.

## Ambisonics {#ambisonics}

[]{.ix t="Ambisonics"}[]{.ix t="AmbiX"}Ambisonics records a whole sphere of sound, independent of any speaker layout. It can then be turned, mirrored, and decoded to headphones or to any speakers. PHI works in AmbiX (ACN channel order, SN3D normalisation), from first to third order for processing and up to tenth order for metering.

| Command | Keys | Does |
|---|---|---|
| Encode to Ambisonics | `Ctrl+Alt+Shift+E` | Places a mono or stereo sound in the sphere, with the same head view as Binaural |
| Rotate / Mirror Scene | `Ctrl+Alt+Shift+Y` | Yaw, pitch and roll; swaps left and right, front and back, or up and down; on the selection or the whole file, with a live preview |
| Decode Ambisonics | `Ctrl+Alt+Shift+D` | To headphones (binaural, through any HRTF set) or to speakers |
| FuMa to AmbiX, AmbiX to FuMa | | Converts older B-format files |
| Ambisonic Scene | `Ctrl+Alt+Shift+A` | A map of the whole sphere at the playhead, glowing where the sound is |

[]{.ix t="Decode Ambisonics"}[]{.ix t="AllRAD"}**Decoding to speakers** covers Stereo and Quad through 5.1, 5.2, 7.1, 7.2, 7.1.2, 7.1.4 and 7.2.4 to 9.1.6, and your own arrays. PHI uses AllRAD with max-rE weighting, a decoder that works well even for uneven layouts. Subwoofer channels are left silent for your bass management.

An ambisonic file plays decoded to your speakers. Files named with AmbiX, FOA, HOA or B-format open as Ambisonics; mark others with Channel Layout.

![The Ambisonic Scene: the whole sphere unrolled, glowing where the sound is.](images/ambi-scene.png)

## Speaker Arrays {#speaker-arrays}

[]{.ix t="Speaker Arrays"}Spatial > Speaker Arrays describes your own speaker setup: up to 16 channels in the order of your interface's outputs, each with a name, azimuth, elevation and whether it is a subwoofer. It is shown from above and from the side, and rings, domes and a cube give you a start.

![Speaker Arrays: an octagon to start from, seen from above and from the side.](images/speaker-arrays.png)

Decode Ambisonics and the Ambisonic Studio's master bus list each array as Custom: its name, and fit an AllRAD decoder to it.

## The Ambisonic Studio {#ambisonic-studio}

[]{.ix t="Ambisonic Studio"}[]{.ix t="Envelop for Live"}Spatial > Ambisonic Studio (`Ctrl+Alt+Shift+H`) brings the devices of [Envelop for Live](https://github.com/EnvelopSound/EnvelopForLive) (E4L) together in one floating panel that stays open while you work. It was written for PHI from E4L's documentation: no Max for Live, no Ableton and no E4L code are needed.

![The Ambisonic Studio: the rack, the chosen device with its display, and the master bus.](images/ambisonic-studio.png)

Build a **rack** on the left, set the chosen device in the middle with its **display** (the room from above and from the side; drag a panner's sound), and use the **master bus** on the right.

### Sources and voices

| Device | Does |
|---|---|
| Source Panner | Mono or stereo, polar or XYZ, radius with E4L's distance model and range |
| Mono Panner, Stereo Panner | With azimuth, elevation and spread LFOs, free or in time with the tempo |
| Quad Panner | LR, X and W spread modes |
| Omni Panner | W only |
| Multi-Panner | Up to 16 channels of the file as a group, with global, scale and per-source offsets |
| Spinner | Spins the sound round the listener |
| Multi-Delay | Spiral or pong, synced, azimuth locked to the panner |
| Brownian Delay | Eight wandering voices in a box or wedge, delayed and frequency-shifted |
| Delay Boids | A flock of delays |
| AuraVerb | A first-order reverb with two-band decay, EQ, compressor and gate |
| B-Format Input | An ambisonic file, AmbiX or FuMa |

### Scene effects

| Device | Does |
|---|---|
| HOA Transform | Yaw, pitch, roll and order weights |
| B-Format Convolution Reverb | A 4-channel B-format impulse response, or W/X/Y/Z files, or a built-in hall; size, rotation, early and late, four EQ stages, distance lock |
| Spatial Slicer | A slice of the sphere cut in and out on tempo triggers |
| Gain | |

### The master bus

First to third order, the scene's channel meters, an HOA scope, and the decoder: AmbiX, FuMa, headphones (binaural) or any speaker layout. **Preview** loops the selection (or the first 15 seconds) and follows every change, heard on headphones, stereo speakers or the decoder's outputs 1 and 2. **Apply** renders the whole file with a tail for echoes and reverb, as an undoable edit.

Racks are kept between sessions, can be saved and loaded as `.json`, and there are factory presets.

::: note
E4L's Ableton routing devices (Aux and 3OA Plugin send and return) and the EnvelopLX lighting and OSC devices are not part of the Studio yet.
:::

::: phi
The Studio's direction-shaped effects, and every AllRAD decoder, are built on points spread over the sphere by the golden angle. See [Where PHI turns by the golden angle](#golden-angle).
:::
