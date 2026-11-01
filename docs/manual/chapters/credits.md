# Credits and Open-Source Software

PHI stands on the work of many people who gave their code away. This appendix lists every piece of open-source software PHI uses, and every project that was read for ideas, with its licence and what it does in PHI. The licence texts are installed with PHI, the stock plugins' in `lib/phi/stock/licenses`.

It comes in two halves:

- **Used in PHI**: code, plugins, fonts and data that are built into PHI, installed with it, or loaded by it.
- **Consulted**: projects whose design, documentation or behaviour PHI's own code was written from, without copying any of their code.

The developers' full record, with the exact versions, the papers and books behind the DSP and the music theory, and the licence check, is `docs/foundations.md` and `docs/licence-compliance.md` in PHI's source.

## PHI

[]{.ix t="Licence"}PHI is free software under the **GNU Affero General Public License, version 3 only** (see the `LICENSE` file). It is built with **JUCE**, under JUCE's AGPLv3 licence, and **Qt**, under the LGPLv3.

## Frameworks and system libraries

[]{.ix t="Open-source software"}PHI uses these from your system (or from the installer), or builds them in when the system has none.

| | By | Licence | In PHI |
|---|---|---|---|
| [JUCE](https://juce.com) 8 | Raw Material Software | AGPL-3.0 | Audio devices, codecs, plugin hosting; with its VST3 SDK (MIT), LV2 (ISC), FLAC and Ogg Vorbis (BSD-3-Clause), zlib, HarfBuzz (MIT) and SheenBidi (Apache-2.0) |
| [Qt 6](https://www.qt.io) | The Qt Company and contributors | LGPL-3.0 | The whole interface |
| [Qt WebEngine](https://doc.qt.io/qt-6/qtwebengine-index.html) (optional) | The Qt Company; the Chromium authors | LGPL-3.0, with Chromium's BSD-3-Clause and others | The web page behind PHI |
| [CLAP](https://github.com/free-audio/clap) | free-audio | MIT | CLAP plugin hosting |
| [LADSPA](https://www.ladspa.org) header | Richard Furse and others | LGPL-2.1-or-later | LADSPA plugin hosting |
| [TagLib](https://taglib.org) | Scott Wheeler and contributors | LGPL-2.1 or MPL-1.1 | Tags of compressed files |
| [libsndfile](https://libsndfile.github.io/libsndfile/) | Erik de Castro Lopo and contributors | LGPL-2.1-or-later | Reading files for Nyquist |
| [PipeWire](https://pipewire.org) | Wim Taymans and contributors | MIT | Recording |
| ALSA and [JACK](https://jackaudio.org) | The ALSA project; the JACK and PipeWire developers | LGPL-2.1-or-later | Audio output |
| [libusb](https://libusb.info) (optional) | libusb contributors | LGPL-2.1-or-later | Push 2 and 3's display |
| [zlib](https://zlib.net) | Jean-loup Gailly and Mark Adler | Zlib | Compressed data |
| OpenGL ([Mesa](https://mesa3d.org) or the driver's) | Khronos; the Mesa developers | MIT (Mesa) | The MilkDrop visualizer |

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
| MIT KEMAR HRTF set | Bill Gardner and Keith Martin, MIT Media Lab | Free to use, citing its authors | Binaural |
| [projectM](https://github.com/projectM-visualizer/projectm) | The projectM team | LGPL-2.1 | MilkDrop |
| [Cream of the Crop](https://github.com/projectM-visualizer/presets-cream-of-the-crop) | Curated by ISOSCELES; presets by their authors, released freely | | MilkDrop presets (an optional download) |
| [FFmpeg](https://ffmpeg.org), with [x265](https://www.videolan.org/developers/x265.html) and [x264](https://www.videolan.org/developers/x264.html) | The FFmpeg and VideoLAN developers | LGPL-2.1-or-later (GPL-2.0-or-later as most distributions build it) | Audio formats, video, streaming; the system's copy |
| [Rubber Band Library](https://breakfastquay.com/rubberband/) | Particular Programs Ltd (Chris Cannam) | GPL-2.0-or-later | Correct Pitch, time stretching |
| STK, inside Nyquist | Perry R. Cook and Gary P. Scavone | STK licence (MIT-like) | Nyquist's physical models |
| [MilkyPlay](https://github.com/milkytracker/MilkyTracker) | Peter "pailes" Barth and the MilkyTracker contributors | BSD-3-Clause | Tracker1's module player |
| [MTS-ESP](https://github.com/ODDSound/MTS-ESP) master library | ODDSound Ltd | Permissive (0BSD-style) | Sending the tuning to MTS-ESP synths. ODDSound's libMTS, which it needs, is downloaded only if you ask; it is not part of PHI |

## Code adapted from other projects

These parts of PHI's source were adapted or ported from others' code, and keep those projects' licences.

| PHI part | From | By | Licence |
|---|---|---|---|
| Tracker1's effect names and conversions, new songs, note entry and block editing | MilkyTracker's `PatternTools`, `ModuleEditor` and `PatternEditor` | Peter Barth | GPL-3.0-or-later |
| The MIDI editor's staff spelling and tuning configs | [Xen Tuner](https://github.com/euwbah/musescore-xen-tuner), a MuseScore plugin, ported to C++, with its whole library of tuning configs (HEJI, Sagittal, ups and downs and more) | euwbah and contributors | GPL-3.0-or-later |
| Key signatures for maqāms, makams and dastgāhs | [LilyPond](https://lilypond.org)'s `arabic.ly`, `turkish-makam.ly` and `persian.ly` | Amir Czwink, Neil Puttock, Adam Good, Kees van den Doel, Werner Lemberg | GPL-3.0-or-later |
| Jeongganbo: the yulmyeong and the sigimsae | [Jeongganbo Editor](https://github.com/depth221/Jeongganbo-editor); [Jeonggan](https://github.com/Tteoieonire/Jeonggan) | Dongha Hwang; Tteoieonire | GPL-3.0 |
| Control surfaces: port names, SysEx, pad and button numbers, palettes (facts only, no Java code) | [DrivenByMoss](https://github.com/git-moss/DrivenByMoss) | Jürgen Moßgraber | LGPL-3.0 |
| The historical twelve-tone rows | [music21](https://github.com/cuthbertLab/music21)'s `serial` module | Michael Scott Asato Cuthbert | BSD-3-Clause |

## Fonts and data

| | By | Licence | In PHI |
|---|---|---|---|
| [Bravura](https://github.com/steinbergmedia/bravura) | Steinberg Media Technologies (Daniel Spreadbury) | SIL Open Font License 1.1 | The music symbols of the MIDI editor's staff |
| TteoiJeonggan, from [Jeonggan](https://github.com/Tteoieonire/Jeonggan) | Tteoieonire | SIL Open Font License 1.1 | The MIDI editor's jeongganbo |
| Inter, Archivo Narrow, Schibsted Grotesk | Rasmus Andersson; Omnibus-Type; the Schibsted Grotesk authors | SIL Open Font License 1.1 | Vaporizer2's skins, in place of fonts that could not be shipped |
| Helm's factory presets | Matt Tytel and the patch authors | CC-BY-4.0 | The Subtractive2 instrument |

## Consulted: ideas only, no code {#consulted}

[]{.ix t="Open-source software!consulted"}PHI's own code was written after reading these projects, their documentation or their output. None of their code is in PHI.

| PHI part | Follows | By | Their licence |
|---|---|---|---|
| Extreme Stretch, the Ambient warp | [Paulstretch](https://hypermammut.sourceforge.net/paulstretch/); [PaulXStretch](https://github.com/essej/paulxstretch) | Nasca Octavian Paul; Xenakios and Jesse Chappell | GPL-2.0-only; GPL-3.0 |
| Master Assistant | [Matchering](https://github.com/sergree/matchering) | Sergree | GPL-3.0 |
| Ambisonic Studio | [Envelop for Live](https://github.com/EnvelopSound/EnvelopForLive); ICST's `ambiencode` | Envelop; Zurich University of the Arts | GPL-3.0; ICST's own |
| The Slicer | Ninjas2 | Clearly Broken Software | GPL-3.0 |
| Nyquist effects | Audacity's Nyquist host | The Audacity team | GPL-2.0-or-later |
| Transport bar, Effect and Tools menus | [Audacity](https://www.audacityteam.org) | The Audacity team | GPL-2.0-or-later |
| Push 2 and 3's display | [Ardour](https://ardour.org)'s Push 2 support; Ableton's Push 2 Display Interface Manual | Paul Davis, Robin Gareus; Ableton | GPL-2.0-or-later; the manual's own |
| Restyle and the scale wheel | MIDIRestyle | Aaron Brata Aditama | MIT |
| Cent-accurate notes, MPE, the interval clock | [XenRoll](https://github.com/ankalot/xenroll) | ankalot | GPL-3.0 |
| Bhatkhande sargam | [Swara Notebook](https://github.com/Studio-kalavati/Swaranotebook-bandish-editor) and its sargam-spec | Studio Kalavati | AGPL-3.0; EPL-2.0 or GPL-2.0-or-later |
| Carnatic swaras | [Note-a-Bhairavi](https://github.com/ragumagu/note-a-bhairavi) | ragumagu | GPL-3.0 |
| Jianpu | [jianpu-ly](https://github.com/ssb22/jianpu-ly) | Silas S. Brown | Apache-2.0 |
| Fugue | Melodizer, foox, CounterPointer, Strasheela, FuX (surveyed) | their authors | GPL-3.0; MIT; others |
| Cubify | [hmt](https://hackage.haskell.org/package/hmt); the OpenMusic reconstruction of *Nomos Alpha* | Rohan Drape; Agon, Andreatta, Assayag and Schaub | hmt's own |
| Stochasticize's sieves | athenaCL; `music21.sieve` | Christopher Ariza | GPL-3.0; BSD-3-Clause |
| Feels and the rhythm generator | Hydrogen's swing and humanize; Music Pattern Generator; Stochas; Subsequence | their authors | GPL-2.0-or-later; GPL-3.0; others |
| The modulators' déjà vu loop | Mutable Instruments' Marbles | Émilie Gillet | MIT |
| How modulation is routed | [Surge XT](https://surge-synthesizer.github.io); [Vital](https://vital.audio) | The Surge Synth Team; Matt Tytel | GPL-3.0 |
| The tracker's keyboard layout | MilkyTracker's FastTracker 2 mode | The MilkyTracker contributors | GPL-3.0-or-later |
| Scala files | The published `.scl` and `.kbm` formats | Manuel Op de Coul | |

Some projects were looked at and left alone because their licences do not allow it: CarMusTy (permission needed for commercial use), DiArMaqAr (CC BY-NC-SA), Mus2 and JapoScore (proprietary), and Swarlipi (no licence). PHI also follows open standards, among them ITU-R BS.1770 and EBU R 128 for loudness, Broadcast Wave, iXML and XMP for metadata, MIDI, MPE and General MIDI, the CLAP, VST3, LV2, LADSPA and Vamp plugin interfaces, SOFA for HRTFs, AmbiX and FuMa for Ambisonics, SMuFL for music fonts, and MTS-ESP.

Commercial programs that shaped features (Adobe Audition, Sound Forge, WaveLab, Ableton Live, Cubase and others) are named only to describe features; no code or assets were taken from them. See [Copyright and Licences](#copyright).

## This manual

This manual was written for PHI and is set in [EB Garamond](https://github.com/octaviopardo/EBGaramond12) by Georg Duffner and Octavio Pardo, [Inter](https://rsms.me/inter/) by Rasmus Andersson, and [JetBrains Mono](https://www.jetbrains.com/lp/mono/) by JetBrains, all under the SIL Open Font License. Its figures were drawn for it, and its screenshots are of PHI itself.


Trademarks are listed in [Copyright and Licences](#copyright).
