# Processing

The Process menu changes the sound of the selection, or of the whole file when nothing is selected. Every process is one undoable step.

## Gain

[]{.ix t="Gain"}Process > Gain (`Ctrl+G`) turns the selection up or down by a number of decibels. For a quick change while listening, use the [gain knob](#gain-knob) or `Ctrl+Shift+Up` and `Ctrl+Shift+Down` (0.5 dB a press).

## Normalize {#normalize}

[]{.ix t="Normalize"}[]{.ix t="Loudness!normalizing to"}[]{.ix t="True peak!limiting when normalizing"}Process > Normalize (`Ctrl+Shift+N`) brings the selection to a level you choose, measured in one of two ways:

- **Peak level (dBFS)**: the loudest sample lands on the target, for example −1 dBFS.
- **Integrated loudness (LUFS)**: the average loudness, as people hear it, lands on the target.

![Normalize to a loudness preset, with the true peak held under −1 dBTP.](images/normalize.png)

For loudness, the **Preset** list has the levels services and broadcasters play at:

| Preset | Target |
|---|---|
| Spotify, YouTube, Tidal, Amazon | −14 LUFS |
| Apple Music | −16 LUFS |
| Podcast | −16 LUFS |
| Broadcast, EBU R128 | −23 LUFS |
| Broadcast, ATSC A/85 (US) | −24 LUFS |

**Match a File** measures another file and uses its loudness as the target, so a track can sit at the same level as one you like.

Turning a quiet file up to a loudness target can push its peaks past full scale. **Limit the true peak to** (on, at −1.0 dBTP, by default) holds the peaks, including the ones that happen between samples, under the ceiling, and then turns up a touch more so the loudness still lands on target.

## Fades {#fades}

[]{.ix t="Fades"}Process > Fade In (`Ctrl+Shift+I`) and Fade Out (`Ctrl+Shift+O`) fade the selection. Four curve shapes are in the Process menu:

| Shape | Sounds |
|---|---|
| Linear | Even; can sound abrupt at the quiet end |
| Equal Power | Natural for crossfades; holds the level up longer |
| Exponential | Quick at first, then slow: a natural fade-out |
| S-Curve | Slow, quick, slow: a smooth fade for music |

To draw a fade with the mouse, use the [fade handles](#fade-handles).

## Reverse, invert and DC offset

[]{.ix t="Reverse"}[]{.ix t="Invert Polarity"}[]{.ix t="DC offset"}

| Command | Keys | Does |
|---|---|---|
| Reverse | `R` / `Ctrl+Shift+R` | Plays the selection backwards |
| Invert Polarity | `Ctrl+Alt+I` | Turns the wave upside down (needed when two mics fight each other) |
| Remove DC Offset | `Ctrl+Alt+D` | Centres a wave that sits above or below zero, which wastes headroom and makes edits click |

## Stereoize {#stereoize}

[]{.ix t="Stereoize"}Process > Stereoize (`Ctrl+Alt+W`) turns mono into stereo, or rebuilds the image of a narrow stereo recording.

![Stereoize, with the Φ switch for golden-ratio allpasses.](images/stereoize.png)

- **Width** sets how wide the result is.
- **Delay** sets the time behind the widening: short for a tight image, longer for a spacious one.
- **Bass** keeps the lows in the middle, below the frequency you choose, so the bass stays solid.
- **Φ** switches from one delay to four golden-ratio allpasses, so the widening adds less colour (see [Widening without combing](#phi-space)).

Whatever the settings, the result folds back to exactly the original when played in mono.

Stereoize is also a live effect: in the Device Chain choose *Add plugin*, then *Stereoize* under *Built-in*, and turn its Width, Delay and Bass knobs while you listen. Rendering a mono file through it makes the file stereo.

## Mid/Side Width {#mid-side-width}

[]{.ix t="Mid/Side Width"}[]{.ix t="Stereo width"}Process > Mid/Side Width (`Alt+Shift+W`, also in the Spatial menu) widens or narrows the stereo image by working on the side, what differs between left and right, and leaving the centre alone.

![Mid/Side Width at 160 %, previewed: the stereo field, with the measures and what they were before.](images/mid-side-width.png)

| Control | Does |
|---|---|
| Width | 0 to 200 %; **Huge** allows 300 % and eases back where the sides would go out of phase |
| Mono below | Makes everything below a crossover (up to 500 Hz) mono, for tight centred bass |
| Generate sides | Makes width from the centre above 150 Hz, which cancels in mono, so it widens mono sounds too; has the Φ switch |
| Air | A high shelf on the sides only, ±6 dB |
| Rotation | Turns the whole image, ±45° |
| Lift | More width on the louder moments |
| Match level | Keeps the loudness the same as the width changes |

The **stereo field** shows the sound as dots, before in grey and after in colour. Drag it sideways for width and up or down to rotate, or use the arrow keys. Beside it are the correlation, the level of the mono sum and the side against the centre, before and after.

**Listen** in Stereo, Mono, Side only or Bass to check each part. **Preview** plays the result, **A/B** switches to the original, and presets keep settings you like. Mid/Side Width is a live effect too, under *Built-in* in *Add plugin*.

## Convert Sample Type {#convert}

[]{.ix t="Convert Sample Type"}[]{.ix t="Sample rate!converting"}[]{.ix t="Bit depth"}Process > Convert Sample Type (`Ctrl+Alt+T`) changes:

- **the sample rate**, with a high-quality windowed-sinc filter, flat to 95 % of the new limit with over 90 dB of alias rejection;
- **the channels**: mix to mono, or make stereo;
- **the bit depth**, to 24, 16 or 8 bits, with TPDF or noise-shaped dither.

The change is applied now, so you hear what will be saved. Markers keep their place in time.

## Generating sound {#generate}

[]{.ix t="Generate"}[]{.ix t="Tone generator"}[]{.ix t="Noise generator"}Effect > Generate makes new sound. It replaces the selection, inserts at the cursor, or makes a new file when nothing is open.

- **Tone** (`Ctrl+Shift+G`): a sine, square (with pulse width), sawtooth or triangle wave at any frequency, or a sweep from one frequency to another. Tones are band-limited, so they do not alias; switch that off to hear the raw shape alias, for teaching.
- **Noise** (`Ctrl+Alt+Shift+G`): white, pink, brown, blue or violet noise, different in each channel or the same.
- **Pluck**, **Rhythm Track** and **Risset Drum**: Nyquist generators (see [Nyquist Effects and Macros](#nyquist-macros)).
