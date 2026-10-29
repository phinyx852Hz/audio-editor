# Third-party code

## libnyquist

`libnyquist/` is Audacity's `lib-src/libnyquist` from Audacity 3.7.9
(github.com/audacity/audacity, tag `Audacity-3.7.9`): Nyquist 3.16 by
Roger B. Dannenberg (Carnegie Mellon University), built on XLISP by David
Betz, with the Nyx embedding interface by Dominic Mazzoni. Licence:
BSD-style, see `libnyquist/LICENSE.txt` and `libnyquist/nyquist/license.txt`.

Changes for PHI:
- Removed the Mac and Windows system folders (`nyquist/sys/mac`,
  `nyquist/sys/win`) and the FFT test programs (`nyquist/ffts/*-testing`,
  `Timing-code`), which are not built.
- Removed two debug `printf` calls in `nyx.c`.
- Audacity's `CMakeLists.txt` is replaced by `cmake/Nyquist.cmake`.

`nyquist-shim/portaudio.h` stands in for PortAudio, which Nyquist only uses
to play sound itself; PHI gets the audio back from Nyquist instead.

## Nyquist runtime and plug-ins

`share/nyquist/` is Audacity 3.7.9's `nyquist/` folder (the Nyquist runtime,
same licence as Nyquist). `share/nyquist-plug-ins/` holds Audacity 3.7.9's
Nyquist plug-ins that are licensed GPL v2 or later (each file names its
author and licence); the GPL v2-only ones are left out. Plug-ins from an
installed Audacity are also found at run time.

## MTS-ESP (ODDSound)

`MTS-ESP/libMTSMaster.h` and `libMTSMaster.cpp` are ODDSound's MTS-ESP master
library (github.com/ODDSound/MTS-ESP, `Master/`), © 2021 ODDSound Ltd, under a
permissive licence (permission to use, copy, modify and distribute for any
purpose, with or without fee; see each file's header), unchanged. It loads
ODDSound's libMTS library at run time when the user has installed it; libMTS is
not part of PHI. `Source/mixer/MtsEsp.cpp` uses it.

## MilkyPlay (MilkyTracker)

`milkyplay/` is MilkyTracker's player library, `src/milkyplay` from
github.com/milkytracker/MilkyTracker at commit e3ceb05 (2026-09-10), by Peter
"pailes" Barth and the MilkyTracker contributors. Licence: New BSD (see the
header of each file); the rest of MilkyTracker is GPL-3.0-or-later.

Changes for PHI:
- Left out `AudioDriverManager.cpp` and the `drivers/` folder; PHI plays the
  mix through its own audio engine. `AudioDriverManager_PHI.cpp` stands in,
  offering only MilkyPlay's null driver.
- Added `version.h`, which MilkyTracker generates at build time.
- MilkyTracker's `CMakeLists.txt` is replaced by `cmake/MilkyPlay.cmake`.
- `PlayerSTD` can hand an instrument's notes to something outside the player
  (`PlayerSTD::ExternalNotes`) instead of starting its sample: the Tracker's
  instruments linked to the Retro Sampler. Marked `PHI` in the source.

`Source/tracker/PatternText.cpp` adapts the effect conversions and names from
MilkyTracker's `tracker/PatternTools.cpp` (Copyright 2009 Peter Barth,
GPL-3.0-or-later).
Its `convertEffectsFromFT2` and `convertVolumeToEffect` are ported from there too.

`Source/tracker/TrackerEdit.cpp` follows MilkyTracker's `tracker/ModuleEditor.cpp`
and `tracker/PatternEditor.cpp` (Copyright 2009 Peter Barth, GPL-3.0-or-later)
for new songs, instrument and sample slots, note entry and block operations.

## DrivenByMoss (control surfaces)

`Source/control/Devices.cpp` and `Source/control/ControlSurface.h` port device
facts from [DrivenByMoss](https://github.com/git-moss/DrivenByMoss) by Jürgen
Moßgraber (© 2017-2026), licensed LGPL-3.0
(http://www.gnu.org/licenses/lgpl-3.0.txt): the MIDI port names each device
shows on Linux, the SysEx that puts Launchpads in programmer mode and Push into
Live mode and writes its colour palette, the Launchkey's DAW-mode messages, the
pad, button, knob and fader numbers, the Novation palette's layout and the LED
channels for flashing and pulsing (from `de.mossgrabers.controller.novation.
launchpad`, `.novation.launchkey` and `.ableton.push`). No Java source is
included; the code is PHI's own. LGPL-3.0 lets it be conveyed under GPL-3.0 and
so combined with PHI's AGPL-3.0.

## Ardour (Push 2 and 3's display)

`Source/engine/PushDisplay.h/.cpp` drive Push's display the way Ardour's Push 2
support does (`libs/surfaces/push2/push2.cc` and `canvas.cc`,
https://github.com/Ardour/ardour, by Paul Davis and Robin Gareus, © 2016-2026,
licensed GPL-2.0-or-later): the USB vendor and product ids, claiming interface
0, the 16-byte frame header, the BGR565 pixels in rows of 1024 with 64 of
filler, sent unXORed to bulk endpoint 1 every 40 ms. No Ardour source is
included; the code is PHI's own. GPL-2.0-or-later can be conveyed under GPL-3.0
and so combined with PHI's AGPL-3.0. Ableton's Push 2 Display Interface Manual
(https://github.com/Ableton/push-interface) documents the same protocol.

## Bravura

`bravura/` is Steinberg's Bravura music font (github.com/steinbergmedia/bravura, commit 37b1943):
`Bravura.otf` from `redist/otf/`, with `OFL.txt` and `FONTLOG.txt`. Licence: SIL Open Font License
1.1. It is built into PHI as a Qt resource for the MIDI editor's staff notation (SMuFL glyphs:
clefs, noteheads, and the accidentals of HEJI, Sagittal, the maqams and the rest). Unchanged.

## TteoiJeonggan

`tteoi-jeonggan/` is the TteoiJeonggan font from Jeonggan, Tteoieonire's jeongganbo editor
(github.com/Tteoieonire/Jeonggan, GPL-3.0; the font OFL-1.1). `TteoiJeonggan.ttf` is the project's
WOFF2 converted to TrueType with fontTools, nothing else changed; `OFL.txt` carries the licence. It
is built into PHI as a Qt resource for the MIDI editor's jeongganbo (the yulmyeong in hanja and the
sigimsae, which the font draws as ligatures).

## Xen Tuner's tuning configs

`share/xen-tuner/` holds Xen Tuner's `tunings/` folder (github.com/euwbah/musescore-xen-tuner,
commit 936c5c6, GPL-3.0-or-later, `share/xen-tuner/LICENSE`); see `share/xen-tuner/README.md`.
