# Built-in and Stock Effects

PHI comes with three families of effects, all under **Built-in** when you add a plugin:

- PHI's own **mastering effects**;
- the **Φ effects**, built on the golden ratio;
- the **stock plugins**, free plugins by other makers, built with PHI from their own source.

## Mastering effects

[]{.ix t="True Peak Limiter"}

| Effect | Controls | Does |
|---|---|---|
| True Peak Limiter | Input, Ceiling (dBTP), Release | A look-ahead limiter that keeps the peaks between samples under the ceiling too |
| Stereoize | Width, Delay, Bass, Φ Golden | Mono into stereo, still mono-compatible (see [Stereoize](#stereoize)) |
| Mid/Side Width | Width, Mono below, Generate sides, Air, Rotation, Lift | Widens or narrows the sides (see [Mid/Side Width](#mid-side-width)) |
| Prism | Split, rays, macros | Splits the sound into rays of plugins (see [Prisms](#prisms)) |

## The Φ effects

Fifteen effects whose times, frequencies, levels and patterns come from the golden ratio: Φ EQ, Φ Echo, Φ Space, Φ Drift, Φ Rhythm, Φ Resonator, Φ Stack, Φ Tame, Φ De-resonator, Φ Phaser, Φ Chorus, Φ Compressor, Φ Multiband, Φ Limiter and Φ Modulation. To see exactly what one does, measure it with the [Plugin Analyzer](#plugin-analyzer). They are described, with the ideas behind them, in [On Φ](#phi); [The Φ effects at a glance](#phi-effects) lists their controls. Five more, Φ Vocoder, Φ Strings, Φ Grains, Φ Double and Φ Trigger, make new sound from the sound they hear: see [Effects that play from what they hear](#driven-effects).

## Effects that play from what they hear {#driven-effects}

[]{.ix t="Effects that play from what they hear"}A send carries sound, not notes, so an ordinary instrument on a return would have nothing to play. These five Φ effects make new sound out of the sound that reaches them. They take their pitch, rhythm and loudness from it, so they work on a return, in a ray of a [return's Prism](#mixer-return-prism), or anywhere else in a chain. Each has a **Mix**: 100 % is only the new sound, which is what a return wants; lower blends it with what came in, for use as an insert.

### Φ Vocoder {#phi-vocoder}

[]{.ix t="Φ Vocoder"}[]{.ix t="Vocoder"}A classic channel vocoder. The sound coming in (a voice, say) is split into **Bands** (4 to 32, from 80 Hz to 12 kHz), and the level of each band opens the same band of a **Carrier**: a saw, a square, noise, or a saw with noise. You hear the carrier speaking with the voice's vowels and rhythm.

- **Note** and **Chord** set what the carrier plays: one note, octaves, a fifth, or a major, minor or sus4 chord on it. With **Follow Pitch** on, the carrier plays the sound's own pitch instead (and the chord on it), so a sung line comes back as a synth line.
- **Formant** moves the carrier's bands up or down against the voice's, by up to an octave: higher sounds smaller, lower sounds bigger.
- **Release** is how quickly a band closes after the voice leaves it: short for crisp words, long for a smeared pad.
- **Sibilance** lets the voice's own highs through, so the consonants (s, t, f) stay clear.

::: tip
For the classic robot voice: Carrier Saw, Chord Single, Bands 16. For a choir: Chord Major, then a Φ Chorus and a little Φ Space after it in the same ray (the preset **Robot Choir**).
:::

### Φ Strings {#phi-strings}

[]{.ix t="Φ Strings"}[]{.ix t="Sympathetic strings"}Sympathetic strings, like a sitar's or a piano with the sustain pedal down. Up to twelve strings are tuned to a **Chord** on a **Note** (Unison, Octaves, Fifths, Major, Minor, Sus4, Major 7, Minor 7, or Φ: the note × φ^n/2^, in no key at all). Whatever you send them makes the strings near its pitches ring.

- **Strings** is how many, up the chord over the octaves.
- **Decay** is how long a string rings, to −60 dB.
- **Brightness** goes from dark, damped strings to bright, open ones.
- **Width** spreads the strings across the stereo field.

Tune the strings to the song's key: a drum loop through Φ Strings tuned to the chord plays that chord in its rhythm.

### Φ Grains {#phi-grains}

[]{.ix t="Φ Grains"}[]{.ix t="Granular"}A live granular sampler. It keeps the last six seconds of what it hears in a loop, and plays short pieces of it, grains, over and over.

- **Density** is grains a second, **Size** their length (10 ms to 1 s), and **Position** how far behind the present they start. **Jitter** scatters that start around it.
- **Pitch** moves the grains up or down (an octave up gives the shimmer of the preset **Plate and Shimmer**), and **Detune** gives each a random pitch around it.
- **Spread** places the grains across the stereo field; **Reverse** plays them backwards.
- **Freeze** stops recording: the loop keeps what it has, and the grains go on playing it after the sound has stopped. Freeze on a held chord to make a pad of it.

### Φ Double {#phi-double}

[]{.ix t="Φ Double"}[]{.ix t="Pitch tracking"}A pitch-tracking double. It follows the pitch of a monophonic line (a voice, a bass, a lead) and plays an oscillator along with it, an **Interval** away (up to two octaves either side), as loud as the line.

- **Wave**: Sine, Triangle, Saw, Square, or Φ (partials at φ^n^ times the note, each 1/φ quieter: a bell-like, inharmonic tone).
- **Snap to Semitones** rounds the pitch to the nearest note; off, it follows every bend and slide.
- **Glide** smooths the moves between notes; **Tone** is a low-pass from dark to open.
- **Certainty** is how sure the tracker must be of a pitch before it plays. Raise it if breaths and consonants make it chirp.

::: note
Pitch tracking needs one note at a time. On chords Φ Double picks one of the notes, or is silent when it cannot tell.
:::

### Φ Trigger {#phi-trigger}

[]{.ix t="Φ Trigger"}[]{.ix t="Drum replacement"}An audio-triggered drum. Each hit in the sound fires a drum voice: Kick, Snare, Clap, Hat, Tom, Rim, or Φ Bell. Use it to double or replace a weak kick or snare, or to turn a beatboxed or tapped rhythm into drums.

- **Threshold** is how loud a hit must be, and **Sensitivity** how sharp its attack must be to count. **Hold** is the shortest time between two hits, so one hit never fires twice.
- **Tune** and **Decay** shape the drum; **Dynamics** is how much the hit's loudness sets the drum's (0 %: every hit the same).

::: tip
Send only the drum you want doubled: a kick track's send into a return with Φ Trigger set to Kick, at Mix 100 %, adds a kick under it that follows every hit.
:::

## Stock plugins {#stock-plugins}

[]{.ix t="Stock plugins"}The stock plugins are built with PHI and open in one **PHI frame**: a header with the plugin's name and credit, **A/B** compare (B starts as a copy of A; Copy puts what you hear onto the other side) and On/Off; input and output meters at the sides; the plugin's own view in the middle; and a footer of knobs for its main controls, which are also the knobs on its Device Chain card. The frame takes its colours from your theme.

![ToneShiftEQ in the PHI frame, with A/B compare and meters.](images/stock-frame.png)

| Plugin | By | What it is |
|---|---|---|
| ToneShiftEQ | brummer10 | A 12-band EQ with dynamic bands |
| Wolf Shaper | Patrick Desaulniers | A waveshaper with a curve you draw |
| LSP Clipper, LSP Multiband Clipper | Vladimir Sadovnikov (LSP) | Clippers, mono and stereo |
| LSP Oscilloscope | Vladimir Sadovnikov (LSP) | An oscilloscope, ×1, ×2 and ×4 |
| ZL Equalizer 2 | ZL Audio | A dynamic EQ with minimum, matched, mixed and zero phase |
| ZL Spectrum Equalizer | ZL Audio | An EQ drawn on the spectrum |
| ZL Compressor | ZL Audio | A compressor |
| ZL Limiter | ZL Audio | A true-peak limiter with up to 32× oversampling |
| ZL Splitter | ZL Audio | Splits mid/side, low/high or transient/steady |
| Airwindows Consolidated | Chris Johnson, Paul Walker | Every Airwindows effect, over 500 of them, in one plugin |

[]{.ix t="Airwindows Consolidated"}**Airwindows Consolidated** has a searchable picker and each effect's notes. Its background takes the frame's colour, so its view looks see-through in the frame; the rest is as Airwindows made it. Among its effects are tape, console and saturation models, dynamics, reverbs and dither.

::: note
The ZL plugins need Clang 17 or later at build time and are left out without it. With `-DPHI_STOCK_PLUGINS=OFF` none of the stock plugins are built.
:::

## Plugins from your system

Any VST3, CLAP, LV2 or LADSPA effect on your system can go in the chain. For a large free set, install `lsp-plugins`; see [Optional extras](#optional-extras).
