# Architectonics

Status: the golden-ratio set designer built (2026-10-29). The second stage, Compose a Song, comes next.

## What the owner asked for (2026-10-29)

"Add to the menu Architectonics." Then: "This is for designing a set in the clip launcher based on
Phi (the golden ratio) that is generative." It is its own menu: "I would prefer it's own menu ...
I tested it on the 13" screen last night and there should still be some room left." Then: one tool
"that could generate a song structure in the clip launcher. It would draw from the Selector,
semi-randomly. It would have some level of logic based on architectonics that draws from the
four major modes of PHI: rhythm, melody, tuning, and harmony."

The owner's research brief divides architectonics into six kinds: structural, temporal,
hierarchical, dynamic, sonic and mathematical. It names the main sources: Caplin's formal
functions, Lerdahl and Jackendoff's grouping and reductions, Hepokoski and Darcy's sonata
theory, Asafiev's form as a process, Kurth's musical energy, Xenakis, Roads and Smalley. It
works on four levels: macro (the work), meso (sections, phrases and formal functions), micro
(motives, cells and gestures) and the sonic substrate. It also gives a blueprint in five parts:
emergence, expansion, disruption, climax and dissolution. This first stage is the mathematical
and dynamic part at the macro and meso levels: proportion by φ, and an energy arch.

## Where it is

**Architectonics** in the menu bar, between Analyze and Master:

- **Design a Set...** shows the plan for the clip launcher's scenes as you change it. It shows
  each section as a block as wide as its bars and as tall as its energy, the scene numbers on
  the blocks, the five parts along the bottom and the golden section as a gold dashed line. A
  list below gives every section's bars, part and scene. **Deal Again** gives a new seed.
  **Play the Set** plays it.
- **Play the Set** (a tick while it plays) launches each section's scene on its first bar and
  stops the tracks on the last. Choosing it again stops following the set, and the clips play
  on. Stopping the transport stops the set too. The status bar shows the set, the section, its
  part, its scene and the bar.
- **Deal a New Set** uses the same design with a new seed. While a set plays, the new one
  starts on the next bar line.
- **Endless: a New Set at the End**: when a set ends, a new one is dealt from its last bar, and
  so on until stopped.

## The design

- **Length**: a Fibonacci number of bars, from 13 to 2584. The dialog shows each length's time
  at the mixer's tempo; 144 bars at 120 bpm in 4/4 is 4:48. Any other length kept in
  settings.ini works too, but then not every section is a Fibonacci number.
- **The golden section**: the set is cut at round(bars/φ), and 144 bars splits into 89 and 55.
  The climax starts where the cut falls, at 0.618 of the way. **Early** puts it at 0.382, with
  the shorter part first. Each part is cut by φ again, and so on, until no section is longer
  than the **longest section** (13 bars to start). A Fibonacci number always splits into the two
  before it, so every section is a Fibonacci number too.
- **Variety** (38% to start): how many of the sections left are cut once more by φ.
- **The seed** decides, at each cut, whether the longer part comes first or second (by a walk
  of steps of 1/φ, as the Φ random selector walks its favourites). It also decides which
  sections are cut once more and where the scene walk starts. A new seed is a step of √7 from
  the last, so no two seeds are the same.
- **The five parts** (the owner's blueprint):
  - **emergence**: the first 1/φ² of the rise.
  - **expansion**: the rest of the rise.
  - **disruption**: the last section before the climax, a contrast with its energy dropped back.
    This only applies when the rise has three sections or more.
  - **climax**: from the golden section for 1/φ of what is left.
  - **dissolution**: after the climax.
- **Energy** (Kurth's energetics: tension building and spending itself): it rises from a quarter
  to the full at the climax. It is held near the full through the climax and falls to near
  nothing at the end.
- **Scene order**:
  - **the arch**: the scenes ranked by how many clips they hold. Each section takes the one its
    energy calls for, sometimes one step off.
  - **Fibonacci**: the k-th section takes the scene F(k) along the launcher, round and round.
  - **the golden walk**: the k-th section takes the scene at frac(start + k/φ) of the way along.

  The **climax** is the fullest scene, the later of equals, or the one chosen. Every order keeps
  it for the section at the golden section, and no scene plays twice running.

## Playing

The set follows the launch clock to the frame. Each section's scene is queued 0.3 s ahead with
`Engine::launchAt` on the exact frame of its bar line, every track at once. A track with an
empty slot in that scene stops on that frame too, since `launchAt` now honours its frame for a
stop. The bars are counted from the last section launched, so a tempo change takes effect from
the next section on. With the transport stopped, Play starts it with the first scene. Otherwise
the set starts on the next bar line.

Kept in settings.ini as architectonics/bars, longest, order, early, climax, variety, seed and
endless. Code: mixer/Architectonics.{h,cpp} (plain C++, tested in CoreTests' testArchitectonics),
ui/ArchitectonicsSection.cpp.

## Next: Compose a Song

The big generator will build the song in the launcher itself:

- Each section's scene will be dealt from the Selector, semi-randomly as the Φ button deals,
  with as many tracks as its energy calls for.
- Its MIDI will be written by the four modes: rhythm from the rhythm library, by density;
  melody in the Mode menu's mode; harmony by the section's function; and a tuning per section.

Open-source references to study for it, by licence: OpenMusic and OM# (the maquette; GPLv3),
IanniX (GPLv3), ossia score (GPLv3), Abjad (GPLv3), mutwo (GPLv3), music21 (BSD),
Strasheela (GPL) and TidalCycles and Strudel (GPLv3, AGPLv3). bach and cage need Max, so they
are reference only. PHI is AGPLv3, which can take GPLv3 code.
