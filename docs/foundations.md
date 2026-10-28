# What PHI is built on

Every piece of open-source software, code, data, standard, paper and book that PHI is built from or
was written from, with its licence and how it reaches the user. The licence check that goes with it
is [licence-compliance.md](licence-compliance.md). The user-facing thanks are in the manual's
[Credits](manual/chapters/credits.md) and the README's License section; this file is the full record
behind them.

## Keeping this file current

Many threads add features at once, so the rules are short:

1. **New build dependency, fetched repository, vendored folder or stock plugin:** add a row to
   sections 2 to 6 in the same pull request. `scripts/check-foundations.py` fails CI when a
   repository URL, pkg-config module, `third_party/` folder or `phi_stock_licence` name in the
   build is missing here.
2. **Code adapted from another project** (even a few functions): give the file an SPDX line with
   that project's licence if it is not AGPL-compatible as written, credit it in a comment, and list
   the file in section 3. The checker also fails for any file under `Source/` that says *adapted
   from*, *ported from* or carries someone else's copyright line and is not named here.
3. **Ideas, documentation or behaviour only, no code:** a row in section 8.
4. **A standard, paper, book or dataset the code follows:** a line in section 9, 10 or 11 (or in
   the feature's own doc under `docs/`, with a line here that points to it).
5. **Before a release or installer:** go through [licence-compliance.md](licence-compliance.md).

Run the check locally with `python3 scripts/check-foundations.py`.

How a part ships, in the tables:

- **static**: compiled into the `phi` binary.
- **system**: a shared library from the user's system (or, in an installer, one we bundle).
- **plugin**: a separate plugin binary under `lib/phi/stock`, loaded into PHI at run time.
- **program**: a separate program PHI starts; it shares no code with PHI.
- **data**: files installed with PHI.
- **download**: fetched only when the user asks; not shipped by us.
- **ideas**: no code or data copied; PHI's own code written from a description.

## 1. PHI

| | |
|---|---|
| Licence | **GNU Affero General Public License v3.0 only** (`LICENSE`; `SPDX-License-Identifier: AGPL-3.0-only` in each file) |
| Copyright | phinyx852Hz (ΦNYX) and contributors |
| Exceptions in `Source/` | `Source/tracker/PatternText.cpp`, `Source/tracker/PatternText.h` and `Source/tracker/TrackerEdit.cpp` are GPL-3.0-or-later (adapted from MilkyTracker, section 3) |
| The Φ mark | PHI's own drawing (`packaging/PHI-MARK.md`) |
| The manual | `docs/manual/`: © phinyx852Hz, all rights reserved (`docs/manual/chapters/copyright.md`), not under the AGPL |
| `phi_dsp` | `Source/core/CMakeLists.txt` builds the Φ effects' DSP as a library for the PHI synth; it is AGPL-3.0-only like the rest, so the synth is too |

## 2. Frameworks and libraries

| Name | Version used | By | Licence | Ships as | Build input | Used for |
|---|---|---|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.15, patched (`cmake/patches/juce-vst3-inactive-buses.patch`) | Raw Material Software (JUCE) | AGPL-3.0 (dual with the commercial JUCE licence; PHI uses the AGPL) | static | FetchContent or `JUCE_DIR` | Audio devices, codecs, plugin hosting (VST3, LV2, LADSPA); no JUCE UI |
| JUCE's bundled code used by PHI | with JUCE | see JUCE's `LICENSE.md` | VST3 SDK MIT; LV2 SDK ISC; FLAC BSD-3-Clause; Ogg Vorbis BSD-3-Clause; zlib Zlib; HarfBuzz MIT-old; SheenBidi Apache-2.0; pslextensions public domain | static | | Plugin formats, FLAC and Ogg |
| [Qt 6](https://www.qt.io) Widgets, Concurrent, OpenGL | 6.4 or later | The Qt Company and contributors | LGPL-3.0-only (or GPL) | system | `find_package(Qt6)` | The whole interface |
| [Qt WebEngine](https://doc.qt.io/qt-6/qtwebengine-licensing.html) (optional) | with Qt | The Qt Company; Chromium authors | LGPL-3.0-only, with Chromium's BSD-3-Clause and many others | system | `Qt6WebEngineWidgets` | Visualizer > Web Page |
| [CLAP](https://github.com/free-audio/clap) headers | 1.2.7 | free-audio | MIT | static (headers) | FetchContent, `CLAP_DIR` or `clap/clap.h` | CLAP hosting (`Source/engine/ClapFormat.cpp`) |
| [FFmpeg](https://ffmpeg.org) | the system's | FFmpeg developers | LGPL-2.1-or-later; GPL-2.0-or-later when built with GPL parts (as most distributions do, with x264 and x265, both GPL-2.0-or-later) | system | `libavcodec` `libavformat` `libavutil` `libswresample` `libswscale` `libavdevice` | Compressed formats, video, streaming, cameras |
| [TagLib](https://taglib.org) | 1.12 or later | Scott Wheeler and contributors | LGPL-2.1-only or MPL-1.1 | system | `taglib` | Tags of compressed formats |
| [libsndfile](https://libsndfile.github.io/libsndfile/) | the system's | Erik de Castro Lopo and contributors | LGPL-2.1-or-later | system | `sndfile` | Nyquist's file reading |
| [PipeWire](https://pipewire.org) client library (optional) | 0.3 | Wim Taymans and contributors | MIT | system | `libpipewire-0.3` | Recording (`Source/engine/AudioCapture.cpp`) |
| [libusb](https://libusb.info) (optional) | 1.0 | libusb contributors | LGPL-2.1-or-later | system | `libusb-1.0` | Push 2 and 3's display |
| ALSA library | the system's | ALSA project | LGPL-2.1-or-later | system (through JUCE) | | Audio output |
| JACK headers and libjack (optional) | jack2 or pipewire-jack | JACK and PipeWire developers | LGPL-2.1-or-later | system, loaded at run time | `jack/jack.h` | JACK output |
| [LADSPA](https://www.ladspa.org) header | the system's | Richard Furse and others | LGPL-2.1-or-later | static (header) | `ladspa.h` | LADSPA hosting |
| [Rubber Band Library](https://breakfastquay.com/rubberband/) | the system's (3.0+), else v4.0.0 | Particular Programs Ltd (Chris Cannam) | GPL-2.0-or-later (or commercial) | system or static | `rubberband`; else https://github.com/breakfastquay/rubberband | Correct Pitch, time stretching |
| [Vamp plugin SDK](https://www.vamp-plugins.org) (host part) | the system's, else commit c3ef877 | Chris Cannam, Centre for Digital Music, Queen Mary University of London | BSD-style (MIT-like); its KissFFT part BSD-3-Clause | system or static | `vamp-hostsdk`; else https://github.com/c4dm/vamp-plugin-sdk | Vamp analysis plugins |
| [libmysofa](https://github.com/hoene/libmysofa) | the system's (1.2+), else v1.3.5 | Symonics GmbH (Christian Hoene) | BSD-3-Clause; its Speex resampler BSD-3-Clause (Xiph.Org, Jean-Marc Valin) | system or static | `libmysofa` | HRTF sets for Binaural |
| [zlib](https://zlib.net) | the system's | Jean-loup Gailly and Mark Adler | Zlib | system | `ZLIB` | libmysofa when built in |
| [projectM](https://github.com/projectM-visualizer/projectm) (optional) | the system's (4.2+), else commit 349b7cc | The projectM team | LGPL-2.1-or-later | system or static | `projectM4`; else https://github.com/projectM-visualizer/projectm | MilkDrop presets in the Visualizer |
| OpenGL (libGL, Mesa or the driver's) | the system's | Khronos; Mesa developers | MIT (Mesa) | system | `OpenGL` | projectM |
| [Nyquist](https://www.cs.cmu.edu/~music/nyquist/) (libnyquist) | 3.16, from Audacity 3.7.9 | Roger B. Dannenberg (Carnegie Mellon University); XLISP by David Betz; Nyx by Dominic Mazzoni | BSD-style (Nyquist and XLISP notices, `third_party/libnyquist/nyquist/license.txt`) | static | `third_party/libnyquist` | Nyquist effects |
| STK (inside Nyquist, `nyqstk/`) | Nyquist's copy | Perry R. Cook and Gary P. Scavone | STK licence (MIT-like); shipped under Nyquist's terms | static | | Nyquist's physical models |
| [MilkyPlay](https://github.com/milkytracker/MilkyTracker) | commit e3ceb05 (2026-09-10), changed for PHI | Peter "pailes" Barth and the MilkyTracker contributors | BSD-3-Clause (New BSD, in each file's header) | static | `third_party/milkyplay` | Tracker1's module player |
| [MTS-ESP](https://github.com/ODDSound/MTS-ESP) master library | `Master/` as XenRoll carries it (libMTS 1.03 era) | ODDSound Ltd | 0BSD-style (permission to use, copy, modify and distribute, in each file's header) | static | `third_party/MTS-ESP` | MTS-ESP output (`Source/mixer/MtsEsp.cpp`) |
| libMTS (ODDSound) | ODDSound's Linux build | ODDSound Ltd | the repository's 0BSD-style licence; closed source | download (Mode › tuning, at the user's request, into `/usr/local/lib`) | | MTS-ESP output; not shipped by us |
| PortAudio stand-in | | PHI | AGPL-3.0-only | static | `third_party/nyquist-shim` | Lets Nyquist build without PortAudio |

## 3. Code by others in this repository

| Where | From | Licence | Changes |
|---|---|---|---|
| `third_party/libnyquist/` | Audacity 3.7.9 `lib-src/libnyquist` | BSD-style (Nyquist, XLISP) | Mac and Windows folders and FFT test programs removed; two debug `printf`s removed; own CMake (`third_party/README.md`) |
| `share/nyquist/` | Audacity 3.7.9 `nyquist/` (the Nyquist runtime in Lisp, and STK's `rawwaves/`) | Nyquist's licence; rawwaves STK's | none |
| `share/nyquist-plug-ins/` (18 files) | Audacity 3.7.9's Nyquist plug-ins, by Steve Daulton, David R. Sky, Dominic Mazzoni and others | GPL-2.0-or-later (each file); the GPL-2.0-only ones were left out | none |
| `third_party/milkyplay/` | MilkyTracker's `src/milkyplay` | BSD-3-Clause | driver manager replaced (`AudioDriverManager_PHI.cpp`), `version.h` added, `PlayerSTD::ExternalNotes` added (marked `PHI`) |
| `third_party/MTS-ESP/` | ODDSound's MTS-ESP `Master/libMTSMaster.{h,cpp}` | 0BSD-style | none |
| `Source/tracker/PatternText.cpp`, `Source/tracker/PatternText.h` | MilkyTracker `tracker/PatternTools.cpp` (Copyright 2009 Peter Barth): effect conversions and names, `convertEffectsFromFT2`, `convertVolumeToEffect` | GPL-3.0-or-later | adapted |
| `Source/tracker/TrackerEdit.cpp` | MilkyTracker `tracker/ModuleEditor.cpp` and `tracker/PatternEditor.cpp` (Copyright 2009 Peter Barth) | GPL-3.0-or-later | adapted |
| `Source/tracker/TrackerEngine.h` | names the MilkyTracker editing it uses (`TrackerEdit.cpp`) | AGPL-3.0-only (PHI's own) | |
| `Source/control/Devices.cpp`, `Source/control/ControlSurface.h` | Device facts from [DrivenByMoss](https://github.com/git-moss/DrivenByMoss) by Jürgen Moßgraber (© 2017-2026): port names, SysEx, pad and button numbers, palettes | LGPL-3.0 (facts ported; no Java code) | PHI's own C++ |
| `cmake/airwindows/airwindows-LICENSE` | Airwindows' licence text | MIT | |

## 4. Stock plugins

Built from a pinned commit of each project by `cmake/StockPlugins.cmake` and installed in
`lib/phi/stock`, each with the licence files listed (into `lib/phi/stock/licenses/<name>/`).

| Plugin (PHI's `phi_stock_licence` name) | Repository and pin | By | Licence | Licence files copied | Also inside |
|---|---|---|---|---|---|
| ToneShiftEQ (`ToneShiftEQ`) | https://github.com/brummer10/ToneShiftEQ 64f4d7f | brummer10 | BSD-3-Clause | `LICENSE`, `libxputty/LICENSE` | libxputty 0BSD; AudioFFT (HiFi-LoFi) MIT; travesty (Filipe Coelho) ISC |
| Wolf Shaper (`WolfShaper`) | https://github.com/wolf-plugins/wolf-shaper d38cc33 | Patrick Desaulniers | GPL-3.0 | `LICENSE`, `dpf/LICENSE` | DPF ISC; Chivo font OFL-1.1 |
| LSP Clipper, Multiband Clipper, Oscilloscope (`lsp-plugins-clipper`, `lsp-plugins-mb-clipper`, `lsp-plugins-oscilloscope`) | https://github.com/lsp-plugins/lsp-plugins-clipper 5177474, -mb-clipper 7579c70, -oscilloscope b6a1300 | Vladimir Sadovnikov | LGPL-3.0-or-later | `COPYING`, `COPYING.LESSER` | the LSP libraries (`make fetch`), same licence |
| QM Vamp Plugins (`qm-vamp-plugins`), x86-64 only | https://github.com/c4dm/qm-vamp-plugins c918afc, with https://github.com/c4dm/qm-dsp e34a3cc and the Vamp SDK c3ef877 | Centre for Digital Music, Queen Mary University of London | GPL-2.0-or-later | `COPYING` | qm-dsp GPL-2.0-or-later with its `ext/` code (KissFFT BSD-3-Clause and others) |
| ZL Equalizer 2, ZL Compressor, ZL Limiter, ZL Splitter, ZL Spectrum Equalizer (`ZLEqualizer`, `ZLCompressor`, `ZLLimiter`, `ZLSplitter`, `ZLSpectrumEqualizer`) | https://github.com/ZL-Audio/ZLEqualizer a518319, ZLCompressor c11049e, ZLLimiter ef15483, ZLSplitter a8f31f6, ZLSpectrumEqualizer 39f2aba | ZL Audio (zsliu98) | AGPL-3.0 | `LICENSE.md` and the `licenses/` folder | their JUCE fork AGPL-3.0; Highway Apache-2.0; KFR GPL-2.0-or-later; Inter OFL-1.1; Material Design Icons Apache-2.0. Their logos are not under the licence and PHI does not show them |
| Airwindows Consolidated (`AirwindowsConsolidated`) | https://github.com/baconpaul/airwin2rack d8fe301, patched (`cmake/airwindows/consolidated-phi.patch`) | Airwindows effects by Chris Johnson; Consolidated by Paul Walker | MIT, built with JUCE so the plugin is GPL-3.0 | `LICENSE.md`, `res/LICENSE.gpl3`, `res/OFL.txt`, `airwindows-LICENSE` | Plus Jakarta Sans and Fira Mono OFL-1.1; clap-juce-extensions MIT |

## 5. Stock instruments

Built by `cmake/Instruments.cmake`, installed beside the stock plugins with their licences.

| Instrument (name in PHI; `phi_stock_licence` name) | Repository and pin | By | Licence | Licence files copied | Also inside |
|---|---|---|---|---|---|
| Granular (`GRNLR`) | https://github.com/passivist/GRNLR 17702ff, ported to JUCE 8 by PHI (`cmake/instruments/grnlr/`) | Raffael Seyfried | GPL-3.0-or-later | `LICENSE.md` | PHI's JUCE (AGPL-3.0) |
| Retro Sampler (`AmiSampler`) | https://github.com/astriiddev/Ami-Sampler-VST 6d76a29, ported to JUCE 8 (`cmake/instruments/ami/`) | Finley Baker (*astriid*) | GPL-3.0 | `LICENSE.txt` | the AmiDOS font (`Res/amidos.ttf`, built into the plugin): its embedded licence is the FontStruct Non-Commercial Licence, see licence-compliance.md. Sample packs not shipped |
| Wavetable (`Vaporizer2`) | https://github.com/VASTDynamics/Vaporizer2 1c56c4b, patched (`cmake/instruments/vaporizer2-bundle-data.patch`), its fonts replaced (`cmake/instruments/vaporizer2-fonts/`) | VAST Dynamics | GPL-3.0 | `LICENSE`, `PHI-FONTS.md`, `OFL-Inter.txt`, `OFL-ArchivoNarrow.txt`, `OFL-SchibstedGrotesk.txt`, `LICENSE-OpenSans.txt` | its own JUCE; the system's FFTW (GPL-2.0-or-later); a starter set of its wavetables, noises and presets (`cmake/instruments/vaporizer2-starter.txt`); fonts built into the plugin: Open Sans (Apache-2.0) and, in place of its SF UI Display, Trade Gothic LT and Alte Haas Grotesk, PHI's Inter, Archivo Narrow and Schibsted Grotesk (OFL-1.1) |
| Modular (`Cardinal`) | https://github.com/DISTRHO/Cardinal 0a530b7, cut down to its host modules and VCV Fundamental (`cmake/instruments/cardinal_foundational.py`), re-skinned (`cmake/instruments/cardinal-skin/phi_skin.py`) | Filipe Coelho (DISTRHO) and contributors | GPL-3.0-or-later | `LICENSE`, `docs/LICENSES.md`, `plugins/Fundamental/LICENSE`, `PHI-ARTWORK.md` | VCV Rack GPL-3.0-or-later; VCV Fundamental GPL-3.0-or-later (panels GPL-3.0-or-later or CC0); Carla GPL-2.0-or-later; QuickJS MIT; jansson MIT, libarchive BSD-2-Clause, libsamplerate BSD-2-Clause, zstd BSD-3-Clause (downloaded by its build). VCV's component library artwork (CC-BY-NC-4.0) is replaced with PHI's |
| Additive (`Overtonium`) | https://github.com/benjamindehli/overtonium 8942247 | Benjamin Dehli | AGPL-3.0 | `LICENSE` | the JUCE it pins (AGPL-3.0) |
| Subtractive1 mono (`CA-72`) | https://github.com/idlefoundry/ca-72 7b8f0ee | Idle Foundry | GPL-3.0-or-later | `LICENSE`, `THIRD-PARTY-NOTICES.txt` | nih-plug ISC, baseview and softbuffer MIT/Apache-2.0, CLAP MIT, clap-wrapper MIT, URW Gothic (AGPL-3.0 with font exception / LPPL), Rust crates (in its notices) |
| Subtractive2 poly (`Helm`) | https://github.com/amilajack/helm ce20c33 (Amila Welihinda's JUCE 9 port), built by PHI's CMake (`cmake/instruments/helm/`) | Matt Tytel | GPL-3.0-or-later | `COPYING` | JUCE 9 AGPL-3.0; factory presets **CC-BY-4.0** (`patches/Factory Presets/LICENSE`); Roboto and Droid Sans Mono Apache-2.0 (`fonts/LICENSE`); mopo GPL-3.0; concurrentqueue BSD/Boost |
| FM (`OctaSine`) | https://github.com/greatest-ape/OctaSine e347a93 | Joakim Frostegård | AGPL-3.0 | `LICENSE` | Open Sans (`contrib/open-sans/LICENSE.txt`); Rust crates (MIT, Apache-2.0 and others) |

## 6. Trackers

| | Repository and pin | By | Licence | Ships as | Notes |
|---|---|---|---|---|---|
| Tracker1 | PHI's own, on MilkyPlay (sections 2 and 3) | | AGPL-3.0-only; MilkyPlay BSD-3-Clause; adapted files GPL-3.0-or-later | static | |
| Tracker2, Furnace (`Furnace`) | https://github.com/tildearrow/furnace f2619db (v0.6.8.3), patched (`cmake/trackers/furnace-opacity-bar.patch`) | tildearrow and contributors | GPL-2.0-or-later | program | its vendored libraries (SDL2 Zlib, fmt MIT, FreeType FTL, libsndfile LGPL-2.1, PortAudio MIT, RtMidi MIT, zlib, FFTW GPL-2.0-or-later, Dear ImGui MIT and many chip emulators under BSD, LGPL, GPL-2.0-or-later, MIT and zlib licences); fonts with their licences in its `papers/` (installed). Demo songs left out (not GPL); its instruments and wavetables are installed |

## 7. Data, presets, fonts and other assets

| What | From | Licence | Ships as |
|---|---|---|---|
| MIT KEMAR HRTF set (`MIT_KEMAR_normal_pinna.sofa`) | Bill Gardner and Keith Martin, MIT Media Lab (1994), in SOFA form from libmysofa | Free to use with no restrictions, provided the authors are cited | data (`share/phi/hrtf`) |
| Nyquist runtime and STK rawwaves | Audacity 3.7.9 (section 3) | Nyquist; STK | data (`share/phi/nyquist`) |
| Audacity's Nyquist plug-ins | section 3 | GPL-2.0-or-later | data (`share/phi/nyquist-plug-ins`) |
| Vaporizer2 starter wavetables, noises and presets | VAST Dynamics | GPL-3.0 (the repository's licence; the files carry none of their own) | inside the plugin bundle |
| Helm factory patches | Matt Tytel and the patch authors | CC-BY-4.0 | inside the plugin bundle |
| Furnace instruments and wavetables | the Furnace project | not stated separately (check) | with Furnace |
| [Cream of the Crop](https://github.com/projectM-visualizer/presets-cream-of-the-crop) MilkDrop presets, and the [MilkDrop texture pack](https://github.com/projectM-visualizer/presets-milkdrop-texture-pack) | curated by ISOSCELES; presets by their authors | no licence; the projectM team treats them as public domain | download (Visualizer, at the user's request) |
| Historical twelve-tone rows (PR #69, `Source/mixer/Serial.cpp`) | music21's `serial` module (`historicalDict`), Michael Scott Asato Cuthbert | BSD-3-Clause (notice in `docs/serial.md` on that branch) | static (data in code) |
| Theme colours, the Φ mark, the manual's figures | PHI | AGPL-3.0-only (the manual: all rights reserved) | data |
| Fonts for Vaporizer2's Tech, Dark and IceCubes skins (`cmake/instruments/vaporizer2-fonts/`) | Inter (Rasmus Andersson and the Inter Project Authors), Archivo Narrow (Omnibus-Type), Schibsted Grotesk (the Schibsted-Grotesk Project Authors), from Google Fonts; static Regular and Bold made with fontTools | OFL-1.1 | inside the Vaporizer2 plugin |
| Manual fonts (web edition loads them from Google Fonts; print uses the system's) | EB Garamond (Georg Duffner, Octavio Pardo), Inter (Rasmus Andersson), JetBrains Mono (JetBrains) | OFL-1.1 | not shipped |

## 8. Written from others' work, no code copied

| PHI part | Follows | Their licence | What was taken |
|---|---|---|---|
| Push 2 and 3's display (`Source/engine/PushDisplay.cpp`) | [Ardour](https://github.com/Ardour/ardour) `libs/surfaces/push2` (Paul Davis, Robin Gareus); Ableton's [Push 2 Display Interface Manual](https://github.com/Ableton/push-interface) | GPL-2.0-or-later; the manual's own | USB ids, frame header, pixel layout and timing |
| Nyquist effects (`Source/core/Nyquist.cpp`) | Audacity's `NyquistBase.cpp` | GPL-2.0-or-later | how the script is set up and its properties named |
| Extreme Stretch, Ambient warp (`Source/core/Paulstretch.cpp`) | [Paulstretch](https://hypermammut.sourceforge.net/paulstretch/) (Nasca Octavian Paul); [PaulXStretch](https://github.com/essej/paulxstretch) (Xenakios, Jesse Chappell) | GPL-2.0-only; GPL-3.0 | the algorithm and the order of the spectral chain; no code (GPL-2.0-only could not be combined) |
| Master Assistant (`Source/core/MasterAssist.cpp`) | [Matchering](https://github.com/sergree/matchering) (Sergree) | GPL-3.0 | how tone and loudness are compared with a reference |
| Ambisonic Studio (`Source/core/SpatialStudio.cpp`) | [Envelop for Live](https://github.com/EnvelopSound/EnvelopForLive) (Envelop); its distance model from ICST's `ambiencode` (Zurich University of the Arts) | GPL-3.0; ICST's own | the devices' documented behaviour |
| Slicer (`Source/sampler/SlicerEngine.cpp`) | Ninjas2 (Clearly Broken Software) | GPL-3.0 | the design |
| Scala tuning files (PR #68, `Source/core/Scala.cpp`) | the published [.scl](https://www.huygens-fokker.org/scala/scl_format.html) and .kbm format descriptions (Manuel Op de Coul) | | the file formats; no Scala code |
| Restyle (PR #68) | MIDIRestyle (Aaron Brata Aditama) | MIT | the idea of moving a melody into another system |
| MIDI editor views and curves (`Source/ui/MidiEditor.cpp`, `Source/mixer/NoteEditor.cpp`) | [XenRoll](https://github.com/ankalot/xenroll) (cent-accurate notes, its clock diagram); MIDIRestyle's scale wheel; Intonara's pitch axis; Cubase's Key Editor | GPL-3.0 (XenRoll); MIT (MIDIRestyle); others their own | ideas only |
| Feels and the rhythm generator (PR #70) | Hydrogen's swing and humanize; Music Pattern Generator; Stochas; Subsequence | GPL-2.0-or-later (Hydrogen); GPL-3.0 (Stochas); others their own | ideas only |
| Fugue (`Source/mixer/Fugue.cpp`) | surveyed: Melodizer (GPL-3.0), foox and CounterPointer (MIT), Strasheela, FuX; unlicensed generators not used | | the textbook form only (`docs/mixer.md`) |
| cubify (`Source/mixer/Cubify.cpp`) | [hmt](https://hackage.haskell.org/package/hmt) (Rohan Drape); Agon, Andreatta, Assayag and Schaub's OpenMusic reconstruction | hmt's own | checked against, no code (`docs/cubify.md`) |
| Stochasticize sieves | Christopher Ariza's athenaCL and `music21.sieve` | GPL-3.0 (athenaCL); BSD-3-Clause (music21) | read for reference; code from Xenakis's texts (`docs/stochastic.md`) |
| Tracker keyboard and layout | MilkyTracker's FastTracker 2 mode | GPL-3.0-or-later | the layout |
| Transport bar, Effect and Tools menus | Audacity | GPL-2.0-or-later | the layout |

## 9. Standards and file formats

- ITU-R BS.1770 (loudness, K-weighting, true peak); EBU R 128, EBU Tech 3341 and 3342 (loudness
  meters and range); ATSC A/85.
- EBU Tech 3285 (Broadcast Wave `bext`), AES46 (`cart`), iXML, Adobe XMP (with IPTC and Dublin
  Core schemas), ID3, RIFF `LIST`/`INFO`, CD-Text.
- MIDI 1.0, Standard MIDI Files (types 0, 1, 2 and RIFF `.rmi`), General MIDI, MPE.
- CLAP, VST3, LV2, LADSPA and Vamp plugin interfaces.
- SOFA (AES69) HRTF files; AmbiX (ACN, SN3D) and FuMa (Furse-Malham) ambisonics.
- Scala `.scl` and `.kbm` (PR #68).
- MTS-ESP (ODDSound's MIDI Tuning Standard, External Shared Platform).
- The freedesktop.org desktop entry, shared MIME info and icon specifications (`packaging/`).

## 10. Algorithms and papers behind the DSP

| PHI part | Source |
|---|---|
| Φ EQ filters (`Source/core/Phi.cpp`), RBJ band-passes | Robert Bristow-Johnson, *Audio EQ Cookbook* |
| Φ Multiband crossovers, Prism splits, mid/side bands | Linkwitz-Riley crossovers (Siegfried Linkwitz and Russ Riley, 1976) |
| Φ Dynamics gain computer (`Source/core/PhiDynamics.cpp`) | Giannoulis, Massberg and Reiss, "Digital Dynamic Range Compressor Design", *JAES* 60 (2012) |
| Mix Assistant (`Source/mixer/MixAssist.cpp`) | Perez-Gonzalez and Reiss, cross-adaptive "equal loudness" automatic mixing |
| Reverb allpasses (`Source/core/Dsp.cpp`) | Manfred Schroeder, allpass reverberators (1962) |
| Sample-rate conversion (`Source/core/Dsp.cpp`) | Kaiser-windowed sinc (James Kaiser) |
| Loudness meters | ITU-R BS.1770, EBU Tech 3341 and 3342 |
| Pitch detection (`Source/core/Pitch.cpp`) | de Cheveigné and Kawahara, "YIN, a fundamental frequency estimator for speech and music", *JASA* 111 (2002) |
| DeClick, DeClip (`Source/core/Restoration.cpp`) | least-squares autoregressive interpolation (Janssen, Veldhuis and Vries 1986; Godsill and Rayner, *Digital Audio Restoration*, 1998); Levinson-Durbin recursion |
| DeHum | Goertzel's algorithm (1958) |
| Oscillators (`Source/mixer/Instruments.cpp`, `Source/core/Generate.cpp`) | PolyBLEP (Välimäki and Huovilainen, 2007) |
| Pink noise (`Source/core/Generate.cpp`) | Paul Kellet's filter |
| Spectrum windows (`Source/core/Windows.cpp`) | fredric harris, "On the Use of Windows for Harmonic Analysis with the Discrete Fourier Transform", *Proc. IEEE* 66 (1978); Albert Nuttall (1981); Dolph-Chebyshev (C. L. Dolph, 1946); Kaiser's window with Kuo and Kaiser's sidelobe fit (1974); multitaper spectra with Slepian's DPSS (David Thomson, 1982; Percival and Walden, *Spectral Analysis for Physical Applications*, 1993); Welch's averaged periodogram (Peter Welch, 1967) |
| Plugin Analyzer (`Source/core/PluginMeasure.cpp`) | exponential sine sweep and deconvolution (Angelo Farina, 2000); 4-term Blackman-Harris window (fredric harris, 1978) |
| Forensic Φ (`Source/core/PhiForensics.cpp`) | Poisson tail tests with a control scan; PHI's own method |
| Master Assistant search | Levenberg-Marquardt with Broyden updates |
| Interaural delay (`Source/ui/Goniometer.cpp`) | Woodworth's spherical-head model (1938) |
| Ambisonic decoding (`Source/core/Ambisonics.cpp`) | AllRAD (Zotter and Frank, 2012), VBAP (Pulkki, 1997), max-rE weighting (Daniel) |
| Sonify's spiral (`Source/core/Sonify.cpp`) | Vogel's sunflower model (Helmut Vogel, 1979) and the golden angle |
| Euclidean rhythms (PR #70) | Bjorklund's algorithm; Toussaint, "The Euclidean algorithm generates traditional musical rhythms" (Bridges, 2005) |
| Key finding (PR #68) | Krumhansl and Kessler's probe-tone profiles (1982) |
| Roughness in the MIDI editor's wheel (`Source/mixer/NoteEditor.cpp`) | William A. Sethares, "Local consonance and the relationship between timbre and scale", *JASA* 94 (1993), fitting Plomp and Levelt, *JASA* 38 (1965) |
| Time stretching (`Source/core/TimeStretch.cpp`) | Rubber Band for most modes (section 2); a phase vocoder for Smooth, overlapping grains for Texture, Paulstretch's algorithm for Ambient |
| Raga strings (`Source/core/PhiSitar.cpp`) | digital waveguide strings (Julius O. Smith) |
| Random numbers (`Source/mixer/Stochastic.cpp`) | splitmix64 and xorshift generators |

## 11. Music theory and scholarship

The feature docs hold the full bibliographies; the main sources are here.

- **Tunings and modes** (`Source/mixer/Tunings.cpp`, `docs/east-asian-tunings.md`, `docs/mixer.md`):
  Pythagorean and five-limit just intonation; the church modes and their hypo-modes; Olivier
  Messiaen, *Technique de mon langage musical* (1944), the seven modes of limited transposition;
  maqāms in IJMES transliteration and Persian dastgāhs in quarter tones; Javanese sléndro and pélog
  from Surjodiningrat, Sudarjana and Susanto's survey of the Central Javanese gamelan (1972);
  Balinese selisir; Shona mbira tunings from Paul Berliner, *The Soul of Mbira* (1978), and Andrew
  Tracey, *African Music* 4(4) (1970); Chinese lǜ from the *Guǎnzǐ*, *Lǚshì chūnqiū*, *Hòu Hànshū*,
  *Suí shū*, Cài Yuándìng (c. 1187) and Zhū Zàiyù (1584, 1596), with Kenneth Robinson (1980), Joseph
  Needham (1962), Yáng Yīnliú (1981) and Fritz Kuttner (1990); Chaozhou and Cantonese modes after
  Mercedes Dujunco and Bell Yung (1989); Japanese scales from Uehara Rokushirō (1895), Koizumi Fumio
  (1958, 1974), Robert Garfias (1975), William P. Malm (1959) and Eta Harich-Schneider (1973);
  Korean yul and tori from the *Akhak kwebŏm* (1493), Robert C. Provine (1988), Lee Hye-ku (1981),
  Lee Bo-hyung, Kim Young-woon (2015) and Keith Howard (2006); Nicolas Slonimsky, *Thesaurus of
  Scales and Melodic Patterns* (1947).
- **Rāgas** (`Source/core/PhiRagas.cpp`, `docs/raga.md`, `docs/raga-darpan.md`): the owner's and their
  teacher's raga sheets; the hundred rare rāgs of Ghulām Ḥaidar, *Sau Achhūp Rāg* (Lahore, 1983);
  Faqīrullāh's *Rāg Darpaṇ* (1666) through Shahab Sarmadee's *Tarjuma-i-Mānakutūhala &
  Risāla-i-Rāgadarpaṇa* (1996) and Raja Man Singh Tomar's *Mānakutūhala*.
- **Rhythms** (`Source/mixer/Rhythms.cpp`, `docs/rhythms.md`): Martin Clayton (2000), Robert
  Gottlieb (1993), David Courtney (1995) and V. N. Bhatkhande's *Kramik Pustak Mālikā* for tāl;
  P. Sambamoorthy, T. Viswanathan and Matthew Harp Allen (2004) and David P. Nelson (2008) for
  tāḷam; Johannes de Garlandia (c. 1240), Richard Hoppin (1978) and William Waite (1954) for the
  rhythmic modes; Godfried Toussaint (2002, 2005, 2019), David Locke (1987) and Kofi Agawu (2016)
  for timelines; Ed Uribe (1993, 1996), Rebeca Mauleón (1993) and Michael Veal (2007) for the
  Caribbean and South America; A. J. Racy (2003) and Deborah Kapchan (2007) for Arabic and
  Maghrebi rhythms; Keith Howard (1995), Wikipedia's *Traditional Korean rhythm* (CC BY-SA) and
  Namuwiki's 국악/장단 (CC BY-NC-SA) for jangdan stroke tables; [Maqam World](https://www.maqamworld.com)
  for iqāʿāt (PR #70); the breakbeats from the records named with each (patterns only, no audio).
- **Counterpoint and fugue** (`Source/mixer/Counterpoint.cpp`, `Source/mixer/Fugue.cpp`): Johann
  Joseph Fux, *Gradus ad Parnassum* (1725); the textbook fugue (subject, answer, countersubject,
  episodes, stretto).
- **Xenakis** (`Source/mixer/Stochastic.cpp`, `Source/mixer/Cubify.cpp`, `docs/stochastic.md`,
  `docs/cubify.md`): Iannis Xenakis, *Formalized Music* (1963, revised 1992), "Sieves",
  *Perspectives of New Music* 28/1 (1990), *Nomos Alpha* (1965-66) and *Psappha*; Thomas DeLio,
  *Journal of Music Theory* 24/1 (1980); Jan Vriend, *Interface* 10 (1981); Messiaen's *Mode de
  valeurs et d'intensités* (1949).
- **Minimalism** (`Source/mixer/Minimalize.cpp`): Steve Reich's phasing (*Piano Phase*, *Clapping
  Music*) and Philip Glass's additive process.
- **Twelve-tone rows** (PR #69): Arnold Schoenberg, "Composition with Twelve Tones" (1941); Milton
  Babbitt, "Some Aspects of Twelve-Tone Composition" (1955), "Set Structure as a Compositional
  Determinant" (1961) and "Twelve-Tone Rhythmic Structure and the Electronic Medium" (1962); Andrew
  Mead, *An Introduction to the Music of Milton Babbitt* (1994); total serialism after Messiaen and
  Boulez; music21's historical rows (section 7).
- **Templates**: ill.Gates' Mudpies and reusable systems (the Creative Organism template).

## 12. Programs that shaped features

Named only to describe a feature; no code or assets taken. All are trademarks of their owners and
PHI is not affiliated with them (`docs/manual/chapters/copyright.md`).

Adobe Audition (Analysis Meters, metadata panel, fades, diagnostics), Sound Forge, WaveLab (album
assembly, overview, rendering), Ableton Live and Push (clip launcher, Device Chain, sends),
Bitwig Studio, Cubase (inserts, iterative quantize), Harrison Mixbus (thumbnails, spatial panner),
REAPER (ambisonic orders), Tracktion, Playtime, iZotope Ozone and Neutron (assistants), OBS Studio,
FastTracker 2, MilkDrop and Winamp, 3D Meter, ab Visualiser, SoundField, PRISM and Kaizen (ambisonic
views), Hyprland (focus behaviour).
