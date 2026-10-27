# 12 tone row

The owner asked on 2026-10-27 for Arnold Schoenberg's serial method, the twelve-tone row, among
PHI's composing tools: looping serial pieces that can be made into a fugue or given counterpoint.
It is our own C++ (mixer/Serial.h), with no code from another program. Every name in the dialog
is lower case (owner).

**12 tone row...** (the owner's name for it) is in the **Mode** menu beside counterpoint, the fugue, stochasticize and
cubify. It needs no clip: the row is its own material.

## The row

- **row**: the twelve pitch classes in order, each once, as note names (`e f g c# f# eb ab d b c
  a bb`, with `#`/`♯` and `b`/`♭`) or numbers (`4 5 7 1 6 3 8 2 11 0 9 10`, 0 is c; `t` and `e`
  may stand for 10 and 11).
- **fill from...**: the MIDI clip shown in the Device Chain (its first twelve different pitch
  classes, the ones it never plays added after them), a row by chance, an all-interval row by
  chance (each of the eleven intervals once, as in Berg's Lyric Suite), a row on one of Milton
  Babbitt's six all-combinatorial **source sets** (below), or the row of a work by Schoenberg,
  Berg or Webern.
- **derive**: Babbitt's derived rows. Type a cell of three or four notes (`c d♭ e♭`) and **derive
  a row**: the cell, then transpositions, inversions and retrogrades of it that fill out the twelve
  notes, so every three (or four) notes of the row are the same shape. A cell none of whose forms
  fill the aggregate says so.
- Under it: the row's intervals, whether it is an all-interval row, and the forms it is
  **hexachordally combinatorial** with: the forms whose first six notes are the six the row's
  first six leave out. Schoenberg built his works on these pairs from op. 25 on (op. 33a's row with
  i5, for one): played together they give all twelve notes in each half. When the row's first
  hexachord is one of Babbitt's source sets it is named too.
- Babbitt's **source sets** ("Set Structure as a Compositional Determinant", 1961) are the six
  hexachords whose rows are combinatorial in all four forms: a (c c♯ d e♭ e f, chromatic), b
  (c d e♭ e f g), c (c d e f g a, diatonic), d (c c♯ d f♯ g a♭), e (c c♯ e f a♭ a, hexatonic) and
  f (the whole-tone scale). *Fill from* draws a row on one: the hexachord transposed and in some
  order, then the other six notes.

## The matrix

The 12 x 12 matrix of the row. Its rows read left to right are the **p** (prime) forms, right to
left the **r** (retrograde) forms; its columns read down are the **i** (inversion) forms, up the
**ri** (retrograde inversion) forms. The labels round it count the level as most textbooks do: p0
is the row as written, i0 its inversion on the same first note, and the number is how many
semitones up from those. **note names** shows the cells as names or numbers.

Clicking a label adds that form to the **plan**.

## The piece

- **plan**: the first voice's forms in order, repeated as long as the piece is (`p0 ri5 i3 r0`).
  Empty, the piece takes a walk through the matrix from p0: each next form is joined to the last
  where it can be by a pivot (its first note the last one's last) or by hexachords that make all
  twelve notes across the join. **walk** writes such a walk into the plan, to edit.
- **voices**: one to four, each a clip on a track of its own: the selected track and the ones
  after it (with the sounds the Selector gave them), then new tracks.
  - **independent**: each voice walks its own path.
  - **canon**: every voice the same forms, entering **delay** sixteenths after the one before
    (dux and comes).
  - **mirror canon**: every other voice inverted (p for i, r for ri, at the same level), as in
    Webern's Symphony op. 21.
  - **combinatorial pairs**: the second voice takes, form by form, the combinatorial partner of
    the first, so the two make all twelve notes in each hexachord at once; a third and fourth voice
    are a second pair on a walk of their own. A row with no partner but its own retrograde is
    paired with that.
  - **array** (Babbitt): block by block, each form of the plan is shared out among the voices
    (lynes), a segment of 12 / voices notes each, the segments passed round so each lyne takes the
    next one in the following block. The lynes start each block together, so every block is an
    aggregate, all twelve notes across the voices. Texture is a line in an array.
- **texture**: a line, or the row cut into dyads, trichords, tetrachords or hexachords, each
  sounding as a chord.
- **rhythm**: **even** (every note one unit), **serial** (each pitch class as many units as its
  place in p0: the row's first note one unit, its last twelve, in every form; total serialism after
  Messiaen and Boulez), **free** (lengths of one to six units, with the chance of a rest after a
  note) or **time points** (Babbitt, "Twelve-Tone Rhythmic Structure and the Electronic Medium",
  1962): twelve units make the modulus (twelve sixteenths, a bar of 3/4, by default), and each note
  starts on the point its pitch class names, counted from the row's first note, the next time that
  point comes round. Each form begins and ends on the modulus.
- **register**: **close** (each note near the last), **wide** (any octave in the range:
  pointillist) or **fixed** (each pitch class in one octave for the whole piece: a frozen register,
  as in Webern). The range is shared out among the voices, the first highest, an octave each at
  least.
- **dynamics**: even, **terraced** (each form a dynamic of its own, ppp to fff) or **serial** (each
  pitch class by its place in i0).
- **length**: in bars of 4/4. The voices hold whole forms of the row only, as many as fit (one at
  least), and the piece is whole bars, so it loops.
- **where**: a **scene in the launcher**, the voices in one scene so they loop together, or the
  **timeline** at the playhead. Every clip plays in 12 equal steps (twelve-tone, every key).
- **then**: counterpoint or a fugue on the first voice, which is shown in the Device Chain.
- **variation**: another number walks another path and draws other registers and rhythms; the
  same number writes the same piece.
- **save as midi...** writes every voice into one MIDI file, each on a channel of its own.

## Into the other tools

PHI's generative chain is the Selector's set, the MIDI generators, cubify, then minimalize. The
12 tone row is one of the generators:

- The voices in a launcher scene are what **cubify...** and **minimalize...** on that scene's
  right-click menu arrange.
- **counterpoint...** and **fugue...** on a voice open in twelve-tone (the clip's 12 equal steps),
  so the new voices may use every key. The fugue's subject is the voice's first phrase, up to four
  bars; one that holds all twelve pitch classes is always answered real, every note a fifth up,
  since a tonal answer would change a note of the row. **Every other middle entry upside down**
  then gives the row's inversion.
- **stochasticize...** can fill its pitch sieve from the voice's notes.

## Sources

- Arnold Schoenberg, "Composition with Twelve Tones" (1941), in *Style and Idea* (1950).
- Milton Babbitt, "Some Aspects of Twelve-Tone Composition" (1955), on combinatoriality; "Set
  Structure as a Compositional Determinant" (1961), on derivation and the source sets;
  "Twelve-Tone Rhythmic Structure and the Electronic Medium" (1962), on time points.
- Andrew Mead, *An Introduction to the Music of Milton Babbitt* (1994), on arrays.
- Babbitt's methods are our own code from these sources: no open-source program for them was found.
- The historical rows are the ones music21's serial module lists (`music21/serial.py`,
  `historicalDict`), used under its BSD 3-Clause licence, whose notice follows:

```
Copyright (c) 2006-2026 Michael Scott Asato Cuthbert

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

Code: mixer/Serial.h and .cpp, ui/SerialSection.cpp; tests in tests/CoreTests.cpp (testSerial).
