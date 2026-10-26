# cubify

Status: built (2026-10-26). Code: `Source/mixer/Cubify.h` (the method, plain C++, with core
tests) and `Source/ui/CubifySection.cpp` (the dialog and placing the result).

## What the owner asked for (2026-10-26)

"Can we create a composing or arrangement tool that would create in the way that this transcript
describes? It would be called "cubify" as a function that would either edit MIDI or better yet
take a scene in the clip launcher (e.g. one that PHI would generate from a Selection set) and
create an auto arrangement based on the Cubify principles in this transcript and any research you
can find." The transcript is a lecture on composing with Xenakis's cubes and "ghost cubes", after
*Nomos Alpha* for solo cello (1965-66). Messiaen, Xenakis's teacher, comes in too: the owner chose
all three of his techniques below.

## Is there open-source software that does this?

Not as an arranger. There are research reconstructions of the group theory only:

- [hmt](https://hackage.haskell.org/package/hmt) (Haskell Music Theory, Rohan Drape) models the
  cube's 24 rotations with Xenakis's labels and his Fibonacci-style chains
  (`Music.Theory.Xenakis.S4`), and his sieves.
- Agon, Andreatta, Assayag and Schaub, "Formal Aspects of Iannis Xenakis' 'Symbolic Music': A
  Computer-Aided Exploration of Compositional Processes" (Journal of New Music Research 33/2,
  2004) rebuilt Nomos Alpha's group logic in OpenMusic.
- Christopher Ariza's athenaCL and music21 (`music21.sieve`) do Xenakis's sieves.

None takes a set of clips and arranges them; cubify is PHI's own, and its code is ours (no port).

## The method

From Formalized Music (chapter VIII, "Towards a Philosophy of Music"), the analyses of Nomos Alpha
by Thomas DeLio (Journal of Music Theory 24/1, 1980) and Jan Vriend (Interface 10, 1981), and the
lecture:

- **The group.** The 24 rotations of a cube, each a way its eight corners change places. Xenakis
  numbers the corners as two tetrahedra, 1-4 and 5-8 (no two corners of one share an edge), and
  labels the rotations I, A, B, C, D, D², E, E², G, G², L, L² (each tetrahedron kept in place: the
  half-turns about the faces and the third-turns about the diagonals) and Q1 to Q12 (the two
  swapped: the quarter-turns and the half-turns about the edges). His table (figure VIII-6) is in
  `Cubify.cpp`; the tests check it is a group, with A·A and Q·Q in the first half and A·Q in the
  second.
- **The path.** Each section's rotation is the product of the two before it. From D and Q12 this
  gives Nomos Alpha's own: D, Q12, Q4, E, Q8, Q2, E2, Q7, Q4, D2, Q3, Q11, L2, Q7, Q2, L, Q8, Q11
  (checked in the tests); E and G2 give a path inside the first half alone.
- **The cubes.** Four cubes nested one in another (the lecture's "ghost cubes", like Russian
  dolls), eight values on each one's corners: pitches, durations, dynamics (ppp to fff) and
  articulations (staccatissimo, staccato, portato, tenuto, legato, accent, marcato, sforzando).
  Each turns on a path of its own: the pitches on D, Q12...; the durations on E, G2...; the
  dynamics against the pitches (their inverses: "the inner cube clockwise, the outer
  anticlockwise"); the articulations on Q12, D.... Reading the corners in order gives a line of
  notes, each the four values that meet at a corner. Each V1 section reads the cube once or more
  (**readings**), turned further each time.
- **Pitches** are fixed: not pitch classes, never moved by an octave. Eight are taken, spread
  evenly, from a sieve over each track's register (two octaves at least). The sieve counts quarter
  tones from C (MIDI key 0), or semitones with quarter tones off; at first it is the Nomos Alpha
  sieve stochasticize also offers, `(13@3 | 13@5 | 13@7 | 13@9) & 11@2 | (11@4 & 13@5) | (13@9 & 11@8) | 13@0 | 13@1 | 13@6`,
  read on the quarter-tone grid, whose period of 143 steps has no octaves in it. The sieves are stochasticize's
  (`stochastic::Sieve`, docs/stochastic.md). Or: the clips' own pitches, filled out from the sieve,
  or a Messiaen mode.
- **Durations** come from the clips' own (onset to onset, on the sixteenths), filled out from 1, 2,
  3, 4, 6, 8, 12 and 16 sixteenths.
- **The form.** 24 sections, V1 V1 V1 V2 six times: Nomos Alpha's 18 sections of cube rotations
  ("path A") and 6 of sieves ("path B"). V2 is freer: a three-quarter-tone scale from each track's
  lowest pitch (minor thirds with quarter tones off), at the extremes of its register (the first
  tetrahedron's tracks low, the second's high), in one to three long notes that swell and slide
  (glissandi); a drum part rolls, swelling and dying away. Each V2 is about as long as the V1
  sections before it.
- **The ensemble.** The tracks sit on the corners of a grand cube: the lower half by register
  (drums first) on the first tetrahedron, the upper half on the second, so that similar tracks
  never share an edge, as the lecturer placed similar instruments apart. The grand cube turns on a
  path of its own (L, Q7...), so in each V1 a track plays the cubes of the corner it has turned
  onto: the oboe plays the trombone's cube. Corners with no track hold ghosts of the nearest
  track's cubes, a fourth to a seventh away. An audio loop has no cubes: it plays in the sections
  its corner turns into the upper tetrahedron and rests in the others, so a Q rotation swaps which
  half of the loops plays.

## The modifications

As the lecture says, the cubes throw up what cannot be played, and the composer modifies it.
cubify does it, counts each, and writes them in the commentary:

- A loud attack (accent, marcato, sforzando) asked for at pp or ppp, like a Bartók pizzicato
  pianissimo: the dynamic raised to mf, or the attack softened to tenuto, by turns.
- A legato or tenuto on a sixteenth, too short to join: staccato.
- A pitch out of the track's range (its clip's register, a fifth beyond each end), when it plays
  another track's cube: the track's own pitch at that corner, or a rest (**out of range**), never
  the pitch an octave away, which would undo the fixed pitches.
- A drum's cube on a melody, or a melody's on drums: the track's own pitch or drum at that corner
  (the lecturer's oboe key clicks for a triangle).
- **balance**: a part more than three steps softer than the loudest it sounds against is brought
  up ("you just won't hear the oboe").
- **hairpins**: a jump of three steps or more in dynamics from one note to the next gets a
  crescendo or diminuendo (expression, CC 11) across the first.
- **room to breathe** (off at first; for winds): after two bars without a rest, a note is cut to
  leave an eighth.
- Bar grouping: every section ends on a bar line (4/4), and the durations sit on the sixteenths.

## Messiaen

The owner chose all three (2026-10-26), and "all of Messiaen's modes in the mode section":

- **The modes of limited transposition** are in the Mode menu (**messiaen**) and the MIDI Clip
  section's mode list; in cubify, **pitches: a messiaen mode** takes one as a sieve (mode 2, the
  octatonic, is `3@0 | 3@1`) on each track's lowest note or a chosen first note. A Messiaen mode
  chosen in the Mode menu starts the dialog on it.
- **Non-retrogradable rhythms**: each reading's eight durations a palindrome.
- **Mode de valeurs**: the path where the four cubes are locked together, as in *Mode de valeurs
  et d'intensités* (1949): each pitch keeps its own duration, dynamic and attack, and only their
  order turns.

## In PHI

- Right-click a scene's name in the launcher: **cubify...** arranges every clip in the scene (a
  scene the Selector or Φ dealt is the intended start). The MIDI Clip section's **cubify...**
  arranges the clip shown, and the Mode menu's **cubify...** does whichever of the two is to hand.
- The result goes onto the tracks' timelines, all starting together after everything already on
  them (as Φ deals onto the tracks): a MIDI file per MIDI track (`<scene> cubify <track>.mid` in
  the mix's Edits folder), and the audio loops cut to the sections they play in. Tab shows the
  tracks. A track with no instrument gets the Synth. Ctrl+Z undoes it.
- Quarter tones are played by pitch bend (± 2 semitones, as the Synth bends): each track plays one
  note at a time, so each note bends on its own, and any instrument with that bend range plays
  them. Leave the track's tuning at 12-TET, or turn quarter tones off.
- `<scene> cubify commentary.txt` beside the MIDI files lists each corner's cubes, each section's
  rotations and the modifications, as a composer's commentary would.
- **variation** 1 follows Xenakis's own paths; other numbers draw other paths (each with at least
  eight different rotations) and other articulation orders.
- The dialog's choices are kept in settings.ini under `cubify/`.
