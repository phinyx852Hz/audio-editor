# Credits

PHI stands on the work of many people who gave their code away. This appendix thanks them, and says under which licence each part comes. The licence texts are installed with PHI, the stock plugins' in `lib/phi/stock/licenses`.

## PHI

[]{.ix t="Licence"}PHI is free software under the **GNU Affero General Public License, version 3 only** (see the `LICENSE` file). It is built with **JUCE**, under JUCE's AGPLv3 licence, and **Qt**, under the LGPLv3.

## Stock plugins

| Plugin | By | Licence |
|---|---|---|
| [ToneShiftEQ](https://github.com/brummer10/ToneShiftEQ), with [libxputty](https://github.com/brummer10/libxputty) | brummer10 | BSD-3-Clause; libxputty 0BSD |
| [Wolf Shaper](https://github.com/wolf-plugins/wolf-shaper), on [DPF](https://github.com/DISTRHO/DPF) | Patrick Desaulniers | GPL-3.0; DPF ISC |
| [LSP Clipper, Multiband Clipper and Oscilloscope](https://lsp-plug.in) | Vladimir Sadovnikov | LGPL-3.0-or-later |
| [ZL Equalizer 2](https://github.com/ZL-Audio/ZLEqualizer), [Spectrum Equalizer](https://github.com/ZL-Audio/ZLSpectrumEqualizer), [Compressor](https://github.com/ZL-Audio/ZLCompressor), [Limiter](https://github.com/ZL-Audio/ZLLimiter) and [Splitter](https://github.com/ZL-Audio/ZLSplitter) | ZL Audio (zsliu98) | AGPL-3.0, with JUCE (AGPL-3.0), Highway (Apache-2.0), KFR (GPL-2.0-or-later), Inter (OFL) and Material Design Icons (Apache-2.0) |
| [Airwindows](https://www.airwindows.com), in [Airwindows Consolidated](https://github.com/baconpaul/airwin2rack) | Chris Johnson; Paul Walker | MIT; built with JUCE, so GPL-3.0; with Plus Jakarta Sans and Fira Mono (OFL) |

The ZL Audio logos are not part of their licence and are not used. Airwindows Consolidated is patched so its background takes the PHI frame's colour; PHI is not affiliated with Airwindows.

## Instruments

| Instrument (name in PHI) | By | Licence |
|---|---|---|
| [GRNLR](https://github.com/passivist/GRNLR) (Granular) | Raffael Seyfried | GPL-3.0-or-later |
| [Ami Sampler](https://github.com/astriiddev/Ami-Sampler-VST) (Retro Sampler) | Finley Baker (*astriid*) | GPL-3.0; its sample packs are not included |
| [Vaporizer2](https://github.com/VASTDynamics/Vaporizer2) (Wavetable) | VAST Dynamics | GPL-3.0, with FFTW (GPL-2.0-or-later) |
| [Cardinal](https://github.com/DISTRHO/Cardinal) (Modular) | Filipe Coelho (DISTRHO) and contributors | GPL-3.0-or-later, on VCV Rack and [VCV Fundamental](https://github.com/VCVRack/Fundamental) (GPL-3.0-or-later) and Carla (GPL-2.0-or-later) |
| [Overtonium](https://github.com/benjamindehli/overtonium) (Additive) | Benjamin Dehli | AGPL-3.0 |
| [CA-72](https://github.com/idlefoundry/ca-72) (Subtractive1 mono) | Idle Foundry | GPL-3.0-or-later |
| [Helm](https://github.com/amilajack/helm) (Subtractive2 poly) | Matt Tytel; JUCE port by Amila Welihinda | GPL-3.0 |
| [OctaSine](https://github.com/greatest-ape/OctaSine) (FM) | Joakim Frostegård | AGPL-3.0 |

VCV Rack's component graphics (CC-BY-NC-4.0) are replaced in Cardinal with PHI's own.

## Trackers

| | By | Licence |
|---|---|---|
| Tracker1, built on [MilkyTracker](https://milkytracker.org) | Peter "pailes" Barth and contributors | MilkyPlay New BSD; PatternTools GPL-3.0-or-later |
| Tracker2, [Furnace](https://tildearrow.org/furnace/) | tildearrow and contributors | GPL-2.0-or-later; built from v0.6.8.3, with a patch adding the opacity bar; its demo songs are left out |

## Libraries and effects

| | By | Licence | In PHI |
|---|---|---|---|
| [Nyquist](https://www.cs.cmu.edu/~music/nyquist/), with XLISP and Nyx, from [Audacity](https://github.com/audacity/audacity) 3.7.9 | Roger B. Dannenberg (Carnegie Mellon University); David Betz; Dominic Mazzoni | BSD-style | Nyquist effects |
| Audacity's Nyquist plug-ins | Steve Daulton, David R. Sky and others | GPL-2.0-or-later | The Effect, Analyze and Tools menus |
| [Vamp plugin SDK](https://www.vamp-plugins.org) | Chris Cannam, Centre for Digital Music, Queen Mary University of London | BSD-style | Vamp analysis |
| [QM Vamp Plugins](https://github.com/c4dm/qm-vamp-plugins) and [qm-dsp](https://github.com/c4dm/qm-dsp) | Centre for Digital Music, Queen Mary University of London | GPL-2.0-or-later | Beats, tempo, key, onsets |
| [libmysofa](https://github.com/hoene/libmysofa) | Symonics GmbH (Christian Hoene) | BSD-3-Clause | HRTF sets |
| MIT KEMAR HRTF set | Bill Gardner and Keith Martin, MIT Media Lab | | Binaural |
| [projectM](https://github.com/projectM-visualizer/projectm) | The projectM team | LGPL-2.1 | MilkDrop |
| [Cream of the Crop](https://github.com/projectM-visualizer/presets-cream-of-the-crop) | Curated by ISOSCELES; presets by their authors, released freely | | MilkDrop presets (an optional download) |
| [FFmpeg](https://ffmpeg.org), with [x265](https://www.videolan.org/developers/x265.html) and [x264](https://www.videolan.org/developers/x264.html) | The FFmpeg and VideoLAN developers | LGPL-2.1-or-later (GPL-2.0-or-later as most distributions build it) | Audio formats, video, streaming; the system's copy |
| [Rubber Band Library](https://breakfastquay.com/rubberband/) | Particular Programs Ltd (Chris Cannam) | GPL-2.0-or-later | Correct Pitch |

The Ambisonic Studio was written for PHI from the documentation of [Envelop for Live](https://github.com/EnvelopSound/EnvelopForLive) by Envelop; it contains none of its code. The transport bar and the Effect and Tools menus follow Audacity's layout.

## This manual

This manual was written for PHI and is set in [EB Garamond](https://github.com/octaviopardo/EBGaramond12) by Georg Duffner and Octavio Pardo, [Inter](https://rsms.me/inter/) by Rasmus Andersson, and [JetBrains Mono](https://www.jetbrains.com/lp/mono/) by JetBrains, all under the SIL Open Font License. Its figures were drawn for it, and its screenshots are of PHI itself.


Trademarks are listed in [Copyright and Licences](#copyright).
