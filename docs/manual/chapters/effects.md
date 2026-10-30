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

Fifteen effects whose times, frequencies, levels and patterns come from the golden ratio: Φ EQ, Φ Echo, Φ Space, Φ Drift, Φ Rhythm, Φ Resonator, Φ Stack, Φ Tame, Φ De-resonator, Φ Phaser, Φ Chorus, Φ Compressor, Φ Multiband, Φ Limiter and Φ Modulation. To see exactly what one does, measure it with the [Plugin Analyzer](#plugin-analyzer). They are described, with the ideas behind them, in [On Φ](#phi); [The Φ effects at a glance](#phi-effects) lists their controls.

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
