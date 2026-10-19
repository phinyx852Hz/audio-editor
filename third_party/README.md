# Third-party code

## libnyquist

`libnyquist/` is Audacity's `lib-src/libnyquist` from Audacity 3.7.9
(github.com/audacity/audacity, tag `Audacity-3.7.9`): Nyquist 3.16 by
Roger B. Dannenberg (Carnegie Mellon University), built on XLISP by David
Betz, with the Nyx embedding interface by Dominic Mazzoni. Licence:
BSD-style, see `libnyquist/LICENSE.txt` and `libnyquist/nyquist/license.txt`.

Changes for PHINYX:
- Removed the Mac and Windows system folders (`nyquist/sys/mac`,
  `nyquist/sys/win`) and the FFT test programs (`nyquist/ffts/*-testing`,
  `Timing-code`), which are not built.
- Removed two debug `printf` calls in `nyx.c`.
- Audacity's `CMakeLists.txt` is replaced by `cmake/Nyquist.cmake`.

`nyquist-shim/portaudio.h` stands in for PortAudio, which Nyquist only uses
to play sound itself; PHINYX gets the audio back from Nyquist instead.

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

Changes for PHINYX:
- Left out `AudioDriverManager.cpp` and the `drivers/` folder; PHINYX plays the
  mix through its own audio engine. `AudioDriverManager_PHINYX.cpp` stands in,
  offering only MilkyPlay's null driver.
- Added `version.h`, which MilkyTracker generates at build time.
- MilkyTracker's `CMakeLists.txt` is replaced by `cmake/MilkyPlay.cmake`.

`Source/tracker/PatternText.cpp` adapts the effect conversions and names from
MilkyTracker's `tracker/PatternTools.cpp` (Copyright 2009 Peter Barth,
GPL-3.0-or-later).
