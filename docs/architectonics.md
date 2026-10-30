# Architectonics

Status: built 2026-10-29. Design a Set plays the launcher's scenes as a set designed by the golden ratio. Compose a Song writes a whole song into the launcher.

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

**Compose > Architectonics** (its own menu in the menu bar at first, between Analyze and Master; a submenu of Compose since the same day, docs/compose.md):

- **Compose a Song...** writes a song into the clip launcher's first empty scenes, a scene a
  section (below).
- **Compose a Score...** writes the same composition out from the first bar to the last, for
  notation (below).
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

## Compose a Song

This is one tool that builds a song structure in the clip launcher, as the owner asked. It deals
from the Selector semi-randomly and draws on PHI's four modes. The form is the set's: the same φ
cuts, five parts and energy arch. Every section gets a scene of its own, in order, written into
the first run of empty scenes (32 at most, so choose longer sections for a long song). The
scenes are named "1 emergence", "2 emergence" and so on. **Play the Set** then plays the song,
until a set is designed or dealt from the scenes instead.

- **Who plays** (Kurth's energy, layered as electronic music builds and breaks down): each
  colour of the Selector has a role. As the energy rises they enter in this order: chords, bass,
  rhythm, arpeggio, melody, then the colours that play sounds. The energy decides how many
  play. All of them play at the climax, and they leave again through the dissolution.
- **The roles**: each colour of the Selector gets one. They start as Live Performance names the
  colours: PD chords, ARP arpeggio, LD melody, BA bass, DR rhythm, and FX, VOX and PR a sound
  from the Selector.
  - **A sound from the Selector**: a Φ deal for each of the five parts, so a part's sounds come
    back whenever the part does. **Deal Again** deals afresh.
  - **Rhythm** (the rhythm mode): a rhythm from the library for each part, as dense as the
    part's energy. The disruption is a contrast: as sparse as its energy is high. Choose where
    the rhythms come from: every rhythm that goes round in whole bars, one of the library's
    groups, or PHI's own four on the floor with the hats as dense as the energy. Drums are on
    channel 10.
  - **Bass, chords and arpeggio** (the harmony mode): the chords are built from every other
    note of the mode, a chord a bar, after Caplin's formal functions:
    - the emergence holds the final (I);
    - the expansion moves through a progression (I–IV–V–I, I–VI–IV–V...);
    - the disruption takes an unstable turn that ends on the dominant;
    - the climax has the fullest progression, with sevenths re-struck;
    - the dissolution closes plagally, IV to I.

    Each part keeps the progression the seed picked, moved on a chord each time it comes back
    (Hepokoski and Darcy's rotations). The section before the climax always ends on V, and the
    song ends on I. The bass plays whole bars, then halves, then eighths as the energy rises;
    the arpeggio plays eighths, then sixteenths.
  - **Melody** (the melody mode): a motif for each part, mostly steps with now and then a leap.
    It is laid on each bar's chord, repeated and sequenced as Caplin's basic idea is. It sits
    higher at the climax and lower at the ends, gets sparser as the energy falls, and the loop's
    last note is a cadence on the chord's root.
- **The mode and the tuning**: everything is in the Mode menu's mode on its final (major on c
  until one is chosen), and every MIDI slot plays in the mode's own tuning or the one chosen
  under Mode ▸ tuning (the slot's tuning, for the PHI synth). An equal-tempered mode stays in
  12-TET.
- **The instruments** come from the Selector: a colour's favourite instrument is loaded on its
  track, else the Synth plays its MIDI.
- **MIDI loops** of 1, 2, 4 or 8 bars, 4 to start; a rhythm's cycle may be longer than the loop.

**Ctrl+click on the Selector's Φ button** (owner, 2026-10-29: "holding ctrl + clicking on the
Phi button generates a random Architectonics composition in the clip launcher") composes a new
song at once, with no dialog. It uses the last settings and roles with a new seed, and it is
also on the Φ button's right-click menu. A plain click still deals one scene. Each scene's
MIDI clips open in the MIDI editor as any launcher clip does (and in its staff view, once that
lands), to edit.

The dialog shows the form as Design a Set does, and lists every section with its part, chords,
players and rhythm. **Compose** writes the song, and **Compose and Play** writes it and plays it.
Kept in settings.ini as architectonics/roles, loop and rhythms, beside the set's.

Code: `architectonics::compose` (plain C++, tested in testArchitectonics), FindPanel::pickDeal (a Φ
deal's picks without dealing them), and MixerWindow::colourTracks, emptyScenes and composeScene.

## Compose a Score

The owner asked for "a staff notation feature that would generate, based on PHI that draws from
the Selector's favorites, a composition based on architectonics function". It is the Compose a
Song dialog with the same form, roles and mode, but it writes one file instead of scenes.

- Each colour that writes notes is a part, from the first bar to the last. Its scene's loop is
  repeated through every section it plays in, and stays silent where it does not.
- Each part has its own MIDI channel; the rhythm stays on channel 10. A marker starts each
  section ("1 emergence", "12 climax"). The Selector's sounds have no notes, so they are left
  out.
- **Open the Score** opens it in the MIDI editor, in the mode's tuning, so its rows and spelling
  are the mode's. Once the editor's staff view lands (the staff notation work), the score will
  open there, with each channel a voice. Kept with OK, the score goes on a new track named for
  it (or the selected one when the mixer is full), from the start, the track tuned to the mode.
- **Save as MIDI...** saves the score as a type 0 file.

Code: `architectonics::parts` and `architectonics::score`, MixerWindow::openScore.

## Open-source references

These are open-source programs to study for the next steps, with their licences. Nothing is
ported yet, and each licence is to be checked before any code is:

- OpenMusic and OM#: the maquette, a temporal design of programmed objects (GPLv3).
- IanniX: Xenakis's UPIC as trajectories (GPLv3).
- ossia score: nested, branching and conditional timelines (GPLv3).
- mutwo: hierarchical events (GPLv3).
- Abjad: algorithmic notation (GPLv3).
- music21: analysis and corpora (BSD).
- Strasheela: constraint-based composition (GPL).
- TidalCycles (GPLv3) and Strudel (AGPLv3): pattern transformation.
- bach and cage need Max, so they are reference only.

PHI is AGPLv3, which can take GPLv3 code.
