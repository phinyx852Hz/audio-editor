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
