# Licence compliance check

Checked on 2026-10-27 against `main` at fad2f53 and the open pull requests #68, #69 and #70, for two
steps: making the repository public, and shipping installers (Linux first, then Windows and macOS).
The parts and their licences are listed in [foundations.md](foundations.md). Run this check again
before every release; the [release checklist](#release-checklist) at the end is the short form.

This is an engineering review, not legal advice.

## Verdict

- **PHI's licence works.** PHI is AGPL-3.0-only. Everything compiled into it or loaded into its
  process is under a licence that can be combined with the AGPL v3: permissive (MIT, BSD, ISC,
  Zlib, Apache-2.0), LGPL (2.1 or 3), GPL-2.0-or-later, GPL-3.0(-or-later) and AGPL-3.0. GPL v3 and
  AGPL v3 allow combination with each other (section 13 of each), and the "or later" GPL v2 parts
  are used under v3. Nothing GPL-2.0-only is combined: Paulstretch's code was kept out on purpose,
  and so were Audacity's GPL-2.0-only plug-ins.
- **Making the repository public: nothing blocks it.** B1 (commercial fonts in Vaporizer2, which
  CI uploads as a downloadable artifact) is fixed in this pull request. Everything in the
  repository itself is fine to publish.
- **Installers: four things to fix first** (B2 to B5): Ami Sampler carries a font we may not
  redistribute, the corresponding source of the bundled plugins must be published with each
  release, the licence notices must be complete, and the About box needs the AGPL's legal notices.

## How the licences fit together

| Licence family | Parts | With PHI's AGPL-3.0-only |
|---|---|---|
| MIT, BSD-2/3-Clause, ISC, 0BSD, Zlib, Apache-2.0 | CLAP, JUCE's VST3 SDK and codecs, PipeWire, libmysofa, Vamp SDK, MilkyPlay, Nyquist, ToneShiftEQ, Airwindows, music21 data | Yes. Keep their copyright and licence notices with binaries |
| LGPL-2.1 / LGPL-3.0 | Qt, TagLib, libsndfile, libusb, ALSA, JACK, FFmpeg (LGPL build), projectM, LSP plugins, DrivenByMoss (facts only) | Yes. Static linking (projectM's pinned build) is fine because PHI's whole source is available, so users can relink |
| GPL-2.0-or-later | Rubber Band, FFmpeg (GPL build), x264, x265, QM Vamp Plugins, Audacity's Nyquist plug-ins, Furnace, Carla, FFTW, KFR | Yes, used under GPL v3 |
| GPL-3.0 / GPL-3.0-or-later | MilkyTracker's adapted code, Wolf Shaper, Airwindows Consolidated build, GRNLR, Ami Sampler, Vaporizer2, Cardinal, CA-72, Helm | Yes, by section 13 of the GPL v3 and the AGPL v3 |
| AGPL-3.0 | JUCE (as PHI uses it), ZL Audio plugins, Overtonium, OctaSine | Yes, the same licence |
| GPL-2.0-only | Paulstretch, some Audacity plug-ins | **No**: none is used (ideas only) |
| Ideas only, no code | Cubase's Modulators panel (the set of modulators); Mutable Instruments' Marbles (MIT; its déjà vu loop, reimplemented in `Source/core/Modulators.cpp`); Surge XT and Vital (GPL-3.0; how modulation is routed) | Nothing to ship: no code was copied |
| CC-BY-4.0 | Helm's factory presets | Yes for data: credit and the licence notice must go with them |
| OFL-1.1 | fonts inside Wolf Shaper (Chivo), Airwindows, ZL Audio (Inter) and OctaSine (Open Sans) | Yes: ship the OFL text |
| Non-commercial or proprietary | VCV's component artwork (replaced), Ami's sample packs (left out), Furnace's demo songs (left out), Vaporizer2's SF UI Display, Trade Gothic LT and Alte Haas Grotesk (replaced), **the AmiDOS font inside Ami Sampler (still in)** | **No**: must not ship |

Furnace runs as a separate program, so its licence does not combine with PHI's; it is shipped
beside it ("aggregate"), which any licence allows. The stock plugins are separate binaries but are
loaded into PHI's process, so they are treated as combined above; all of them are compatible.

## Must fix

### B1. Vaporizer2 builds commercial fonts into its plugin (fixed)

Vaporizer2's `CMakeLists.txt` puts eight fonts from `VASTvaporizer/Resources/Fonts/` into its binary
with `juce_add_binary_data`. Its README says only "to the best of our knowledge the true type fonts
... are freeware and can be distributed". Two of them are not:

- `SFUIDisplay-Regular.ttf`, `SFUIDisplay-Bold.ttf`: Apple's San Francisco font, whose licence allows
  it only for mock-ups of software for Apple platforms, with no redistribution.
- `Trade Gothic LT.ttf`, `Trade Gothic LT Bold.ttf`: a commercial Linotype (Monotype) typeface.
- `AlteHaasGroteskRegular.ttf`, `AlteHaasGroteskBold.ttf`: freeware by Yann Le Coroller, with no
  clear terms for redistribution.
- `OpenSans-*.ttf`: Open Sans, OFL-1.1 or Apache-2.0, fine.

Each pair is one skin's font: Classic uses Open Sans, Tech SF UI Display, Dark Trade Gothic LT and
IceCubes Alte Haas Grotesk.

Fixed: `cmake/instruments/vaporizer2-fonts/ReplaceFonts.cmake` copies free fonts over the six files
before Vaporizer2 builds, under the same names, so its code loads them unchanged: Inter for Tech,
Archivo Narrow for Dark and Schibsted Grotesk for IceCubes (all OFL-1.1). Classic, the default
skin, keeps Open Sans. The fonts' licences and `PHI-FONTS.md` are installed in Vaporizer2's licence
folder. If an update of Vaporizer2 renames a font, the step stops the build instead of shipping it.

### B2. Ami Sampler's AmiDOS font is under a non-commercial licence

`Res/amidos.ttf` is built into the plugin (`cmake/instruments/ami/CMakeLists.txt`,
`juce_add_binary_data`). The font was made by Finley Baker, Ami Sampler's own author, on
FontStruct, and its embedded licence is the **FontStruct Non-Commercial Licence**, which conflicts
with the GPL's freedom to sell copies. The repository as a whole says GPL-3.0, so the author very
likely meant it to be GPL, but the file says otherwise.

Fix: ask Finley Baker (github.com/astriiddev) to confirm the font is GPL-3.0 (or to relicense it
OFL), and keep the answer with the licences; or patch Ami to draw text with an OFL pixel font.

### B3. Publish the corresponding source of every bundled binary with each release

The GPL v3 and AGPL v3 require whoever ships a binary to make its exact source available. For PHI
itself the public repository at the release tag does this. The stock plugins, instruments and
Furnace are built from other people's repositories at pinned commits, with PHI's patches, and
Cardinal's build also downloads jansson, libarchive, libsamplerate and zstd. Those upstream
repositories are not ours and could disappear.

Fix: with each installer, publish one source archive holding PHI at the tag plus every checkout in
the build's `*-prefix/src` folders (with submodules) and Cardinal's downloads, and say in the
installer's notices where it is. A release script can make it from the build folder.

### B4. Ship every licence notice with the binaries

BSD, MIT, ISC, Apache-2.0, OFL and CC-BY all ask for their notice to go with binary copies. Today
the install has `share/licenses/phi/nyquist-license.txt` and the stock plugins' `licenses/` folders.
Missing:

- **In `phi` itself:** JUCE's `LICENSE.md` and the notices of the JUCE parts PHI compiles in (VST3
  SDK MIT, LV2 ISC, FLAC, Ogg Vorbis, zlib, HarfBuzz, SheenBidi Apache-2.0); MilkyPlay's BSD notice;
  the CLAP licence (MIT); and, when they are built in rather than taken from the system, the Vamp
  SDK's `COPYING`, libmysofa's `LICENSE` (with the Speex resampler's), Rubber Band's `COPYING` and
  projectM's `LICENSE.txt`. Also the GPL v2 text for Audacity's plug-ins, the citation the MIT KEMAR
  set asks for, the line "Nyquist can be obtained free from https://www.cs.cmu.edu/~music" that the
  Nyquist licence requests, and (after PR #69) music21's BSD notice.
- **Stock plugins:** Helm's `patches/Factory Presets/LICENSE` (CC-BY-4.0) and `fonts/LICENSE`;
  Wolf Shaper's `resources/fonts/Chivo/SIL Open Font License.txt`; OctaSine's
  `contrib/open-sans/LICENSE.txt` and a list of its Rust crates' licences (for example from
  `cargo about`); ToneShiftEQ's `EQ12/AudioFFT/COPYING.txt` and `EQ12/vst3/travesty/LICENSE`;
  qm-dsp's `COPYING` and its `ext/` notices; Furnace's
  `extern/*/LICENSE*` files (only its main `LICENSE` and fonts are installed now).

Fix: add the files to the `phi_stock_licence` calls (mind the space in Helm's path) and an
`install(FILES ...)` list for PHI's own, into `share/licenses/phi/`, and generate a single
`THIRD-PARTY-NOTICES` file from them for the installers.

### B5. Show the AGPL's legal notices in PHI

AGPL section 5(d) asks an interactive program to show "Appropriate Legal Notices": a copyright
line, that there is no warranty, that it may be shared under the AGPL, and how to see the licence.
Help > About now says only "Free software under the GNU Affero General Public License v3". Add the
copyright, "version 3 only", "comes with no warranty", a button that opens `LICENSE` and the
third-party notices, and Qt's own About Qt (`QMessageBox::aboutQt`), which LGPL users expect.

## Should fix before a release

- **FFmpeg in installers.** On Linux PHI uses the system's FFmpeg and ships none. An AppImage,
  Flatpak, Windows or macOS installer that bundles it must use a build without `--enable-nonfree`
  (non-free builds may not be redistributed at all), ship its licence (LGPL-2.1, or GPL-2.0 if built
  with x264 and x265) and its source. H.264, H.265 and AAC encoders also carry patent licensing in
  some countries; Flatpak's codec extensions or the user's own FFmpeg avoid shipping them.
- **Qt and Qt WebEngine in installers.** Bundled Qt needs its LGPL-3.0 text and a way to get its
  source (a link to the exact Qt release is enough when it is unmodified). Qt WebEngine adds
  Chromium's notices, which are large; consider leaving the web page backdrop out of installers.
- **Furnace's instruments and wavetables** are installed (`WITH_INSTRUMENTS` and `WITH_WAVETABLES`
  default to on) but Furnace states no licence for them, unlike the demo songs it marks as not GPL.
  Ask upstream or turn both off in `cmake/Trackers.cmake`.
- **Vaporizer2's starter wavetables and presets** carry no licence of their own; they are covered
  by the repository's GPL-3.0. Low risk, worth one line in its licence folder.
- **MilkDrop presets** (Cream of the Crop, the texture pack) have no licence; projectM treats them
  as public domain. Keep them a download the user asks for, never inside an installer.
- **Audacity plug-ins:** `delay.ny`, `pluck.ny` and `rhythmtrack.ny` say "GNU General Public License
  v2.0" in their `$copyright` line, but their header comments say "GPL v2 or later", so they may stay.
- **The manual** is "all rights reserved". That is the owner's choice, but it means distributions
  and others may not pass it on with PHI. If it should ship in packages, a licence such as
  CC BY-SA 4.0 would allow it.
- **Facts from non-free sources.** The jangdan stroke tables come partly from Namuwiki
  (CC BY-NC-SA) and Wikipedia (CC BY-SA), the rare rāgs from Ghulām Ḥaidar's book, the breakbeats
  from records. Rhythms, scales and svaras are facts and can be used; keep their text, scans and
  recordings out of the repository.
- **Trademarks.** Product names are used only to describe features
  (`docs/manual/chapters/copyright.md`). Keep the ZL Audio logos hidden, don't use the VST, VCV or
  Airwindows logos, and keep "PHI is not affiliated" wherever a product is named on the site or in
  stores.
- **Windows and macOS.** JUCE's ASIO SDK is GPLv3 or proprietary and the AudioUnit SDK Apache-2.0,
  both fine; every stock plugin needs its Windows and macOS build checked again for vendored code
  and fonts, the same way as B1 and B2.

## Fixed in this pull request

- B1: Vaporizer2's commercial fonts replaced with OFL fonts.
- README: the sentence "The system's copy is used when installed, otherwise version 4.0.0 is
  downloaded and built in" belonged to Rubber Band but sat in the Paulstretch paragraph.
- Manual credits: the MIT KEMAR row had no licence.
- `packaging/arch/PKGBUILD`: the `license` array now names every licence the package carries.

## Release checklist

Before making the repository public:

- [ ] `python3 scripts/check-foundations.py` passes.

Before each installer:

- [ ] B2 to B5 done.
- [ ] Every stock plugin and instrument still at the commit [foundations.md](foundations.md) names;
      any new one checked for fonts, presets and samples (`git ls-tree -r <commit> | grep -iE
      '\.(ttf|otf|wav|png)$'`) and for vendored code.
- [ ] `THIRD-PARTY-NOTICES` regenerated; the source archive (B3) published next to the installer.
- [ ] Bundled FFmpeg, Qt and other system libraries listed with their licences and sources.
- [ ] This file's date and commit updated.
