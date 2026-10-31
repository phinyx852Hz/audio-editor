<!--
Research > Composition, Compose > Architectonics and Compose > Rhythm Generator (owner,
2026-10-31). Drafted from memory and from PHI's docs/architectonics.md and
mixer/RhythmGenerator.h, before the owner's Wikipedia pastes; to be checked against them.
-->

# Composition / Architectonics

## Architectonics and musical form

Architectonics is the study of a piece's large-scale design: how its sections, proportions and
energy are built to make a whole. The theories PHI draws on include William Caplin's formal
functions (beginning, middle and end at every level, *Classical Form*, 1998), Fred Lerdahl and
Ray Jackendoff's grouping and reductions (*A Generative Theory of Tonal Music*, 1983), James
Hepokoski and Warren Darcy's sonata theory (*Elements of Sonata Theory*, 2006), Boris Asafiev's
form as process and Ernst Kurth's musical energy, and, for sound itself, Xenakis, Curtis Roads
(*Composing Electronic Music*, 2015) and Denis Smalley's spectromorphology.

*In PHI*: Compose > Architectonics. Design a Set plans a set from the clip launcher's scenes;
Compose a Song writes one into the launcher a scene a section, drawing sounds from the Selector
and MIDI from the Mode menu's rhythm, melody, tuning and harmony; Compose a Score writes the same
composition out for notation.

- [Musical form](https://en.wikipedia.org/wiki/Musical_form) and
  [Generative theory of tonal music](https://en.wikipedia.org/wiki/Generative_theory_of_tonal_music),
  Wikipedia.
- William E. Caplin, *Classical Form* (Oxford University Press, 1998).
- Fred Lerdahl and Ray Jackendoff, *A Generative Theory of Tonal Music* (MIT Press, 1983).
- Denis Smalley, "Spectromorphology: Explaining Sound-Shapes", *Organised Sound* 2/2 (1997).

## The golden section in form

Composers and analysts have long looked for the golden ratio (about 0.618) in musical form,
the point where a climax falls or a section turns. Ernő Lendvai claimed it throughout Bartók's
music, as in the first movement of *Music for Strings, Percussion and Celesta* (1936), though
later scholars, Roy Howat among them for Debussy, have questioned how far it was intended and
how much the counting decides. Xenakis used the ratio and Fibonacci numbers in *Metastaseis*,
and so did Stockhausen and Sofia Gubaidulina, who planned whole works by Fibonacci series.
Because each Fibonacci number is the sum of the two before it, a Fibonacci length can be cut by
φ again and again into Fibonacci lengths.

*In PHI*: Design a Set gives the set a Fibonacci number of bars, puts the climax at the golden
section (or early, at 0.382), cuts each part by φ again until no section is too long, and
orders the scenes through five parts, emergence, expansion, disruption, climax and
dissolution; Play the Set launches them, and Endless deals a new set at the end.

- [Golden ratio](https://en.wikipedia.org/wiki/Golden_ratio) and
  [Ernő Lendvai](https://en.wikipedia.org/wiki/Ern%C5%91_Lendvai), Wikipedia; see also
  Research > Φ > The golden ratio in music.
- Ernő Lendvai, *Béla Bartók: An Analysis of His Music* (Kahn & Averill, 1971).
- Roy Howat, *Debussy in Proportion* (Cambridge University Press, 1983).

# Composition / Rhythm Generator

## Euclidean rhythms and polymeter

A Euclidean rhythm spreads a number of hits as evenly as possible over a number of steps. Godfried
Toussaint showed in 2005 that the rule, the same as Euclid's algorithm for the greatest common
divisor and Eric Bjorklund's algorithm for timing neutron accelerators, generates many of the
world's traditional timelines: E(3,8) is the Cuban tresillo, E(5,8) the cinquillo, E(7,12) the
West African bell pattern of bembé, and E(5,16) the bossa nova. Rotating the pattern starts it
on another step. Lanes of different lengths run against each other, which is polymeter, and
meet again only after the least common multiple of their lengths.

When the hits divide the steps exactly, as 4 in 16, they fall every n/k steps. When they do not,
the gaps must be of two sizes, and the point is to spread those sizes evenly too. A naive
spacing of 5 in 13, for example, bunches the short gaps at the start: x.x.x..x..x.. Toussaint's
method takes the steps through Euclid's remainders (13 = 2×5 + 3, 5 = 1×3 + 2, 3 = 1×2 + 1):

1. Write the five hits, then the eight rests, in one row.
2. Fold the leftovers on the right under the row, again and again, as long as more than one
   column is left over.
3. Read the columns off in order. The result is x..x.x..x.x.., with its gaps of 3 and 2 as evenly
   mixed as they can be.

Bresenham's line-drawing algorithm gives the same patterns. Other examples:
- E(2,4) is the rock backbeat and E(3,4) cumbia's figure;
- E(2,5) is the meter of "Take Five", and E(4,7) the Bulgarian râčenica;
- rotated, E(7,12) is the Ewe bell and E(5,16) bossa nova.

Some Indian tālas are among the few rhythms it does not produce. Euclid's algorithm turned up in
music theory before, in tuning: Conrad Henfling used it in letters to Leibniz in the
17th century, and Viggo Brun used it to build scales of up to four step sizes. Erv Wilson took
the idea further, and Kraig Grady applied it to long rhythmic cycles.

*In PHI*: Compose > Rhythm Generator writes drum lanes, each a Euclidean rhythm (or a written
pattern, or a Xenakis sieve as a mask) of its own length, with a chance for each hit, humanized
loudness and timing, and rules: a fill every few bars, a rising or falling density, skipped
hits and lanes that turn a step. One seed draws every chance, so the same settings write the
same groove.

- [Euclidean rhythm](https://en.wikipedia.org/wiki/Euclidean_rhythm),
  [Tresillo (rhythm)](https://en.wikipedia.org/wiki/Tresillo_(rhythm)) and
  [Polyrhythm](https://en.wikipedia.org/wiki/Polyrhythm), Wikipedia.
- Godfried Toussaint, "The Euclidean Algorithm Generates Traditional Musical Rhythms",
  *Proceedings of BRIDGES* (Banff, 2005), pp. 47–56, and *The Geometry of Musical Rhythm* (CRC Press, 2013;
  2nd ed. 2019).
