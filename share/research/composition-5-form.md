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

Benjamin Wardhaugh shows what Henfling was doing. Intervals add by multiplying their ratios, so
the algorithm runs on intervals rather than numbers, asking how many times a fifth fits into an
octave. Each step takes the smaller interval out of the larger:
- octave less fifth leaves the fourth (4:3);
- fifth less fourth leaves the tone (9:8);
- fourth less two tones leaves the semitone (256:243);
- tone less two semitones leaves the comma.

Before logarithms (1614) there was no exact answer. Stopping early and treating the remainder as
nothing gives an approximation. Taking a tone as exactly two semitones makes the fourth 5
semitones, the fifth 7 and the octave 12, which is the piano's division. Going one, two or three
steps further gives fifths of 24/41, 31/53 and 179/306 of an octave. Divisions of 41 and 53 were
in fact tried on instruments, and 53 is the comma count behind Turkish makam theory. The two uses
meet in one shape. Spread 7 notes as evenly as possible through the 12 semitones and you get the
diatonic scale, whose steps (2 2 1 2 2 2 1, in some rotation) follow the same pattern as E(7,12),
the Ewe bell.

In the extended version of the Banff paper, Toussaint lists dozens of Euclidean rhythms in use:
- **Greece, Bulgaria and Macedonia:** E(2,5), E(4,7) and others;
- **Arab rhythms:** E(5,6) York-Samai, E(5,7) Nawakhat and E(5,9) Agsag-Samai;
- **Turkey:** E(4,9), the aksak;
- **India:** E(3,11) and E(3,14), the clap patterns of savārī and dhamār tāl;
- **Ghana and Central Africa:** E(7,12) among the Ashanti, E(5,12) in the Central African
  Republic, and E(11,24) and E(13,24) among the Aka.

When 2k < n < 3k the result uses only twos and threes, so it is aksak in Brăiloiu's and Arom's
sense, though not every aksak rhythm is Euclidean. The same structure appears in Bresenham's
lines, in leap-year calendars and in the Sturmian words of combinatorics.

Demaine and others proved that the Euclidean rhythms and their rotations are exactly the
maximally even rhythms, in Clough and Douthett's sense (1991), and the complement of one is
another. Long Indian tālas mostly fall outside: of the 35 sūlādi tālas only 4 are maximally even.
Toussaint also sorts them by a property from the study of strings, with a striking result:
- **Euclidean strings:** the ones that are strings in this sense are the ones classical music and
  jazz favour, and they are rare in Africa.
- **Their reverses:** these are the most widely used.
- **The rest:** these split by step sizes. Those of 1 and 2 are African, and those of 2 and 3 are
  found only in Bulgaria.

Viggo Brun, in a ten-minute lecture at the International Congress of Mathematicians (Stockholm,
1962), went back to Euclid's own form of the algorithm. Aristotle called it antanairesis,
"taking away in turn": subtraction, not division. In 1919 Brun had extended it from two numbers
to three, and he applied it to the old problem of dividing the octave into equal steps that
approximate several pure intervals at once. Divisions he discusses:
- **31 steps:** proposed by Vicentino, Mersenne, Huygens and, in his time, Fokker;
- **53 steps:** Mercator's, said to go back to Philolaus and to have been used in China;
- **72 steps:** better than Drobisch's 74, and tried on an instrument by a Russian, J. Mursin.

Erv Wilson's worksheets of 1997 show this with Viggo Brun's version of the algorithm. He starts
with an octave of N steps and a generator, usually the fifth, of G steps, such as 12 and 7. At
each stage he takes the smaller number from the larger, re-sorts, and records how many of each
kind of step the scale has so far. Every stage is a "moment of symmetry" (MOS): a scale with just
two sizes of step. For 12 and 7 the stages give scales of 2, 3, 5, 7 and finally 12 notes:
- the pentatonic is 3 large steps and 2 small;
- the diatonic is 5 and 2;
- the chromatic is all 12 semitones.

The fractions climb toward 7/12, each next one a mediant of two before, as in the Stern-Brocot
"scale tree". He works the same table for octaves of 19, 31, 50, 53, 69 and 88 steps, and for
generators that are thirds (minor, neutral, subminor) rather than fifths. He follows it as far as
1995 steps with a generator of 527, which reproduces the fifth of Larry Hanson's tuning.

In 1995 he applied Brun's three-number form to the octave, fifth and major third together. The
result is a sequence of "constant structures":
- the stages have 1, 2, 3, 5, 7, 10, 15, 22, 34 and 53 notes;
- each splits its steps into smaller just intervals: 3/2 and 4/3, then 9/8, then 5/4 and 16/15,
  then 10/9, 81/80, 25/24 and 128/125.

Toussaint found the link in 2004, two years into studying rhythm mathematically: the two
numbers fed to Euclid's procedure (*Elements* VII.1–2) become the counts of sounded and silent
pulses in a cycle. His later papers went further:
- **What makes a "good" timeline:** why the son clave spread so widely, measured by features
  such as evenness and off-beatness.
- **Families of African ternary bell patterns:** classified and drawn as family trees.
- **The swap distance:** the number of swaps of a neighbouring note and rest needed to turn one
  rhythm into another, with a fast algorithm by Toussaint and Justin Colannino.
- **Flamenco compás (2004–05):** with Spanish colleagues, he used the swap distance to build a
  family tree of the flamenco metres. It placed the fandango, traditionally held to be the
  oldest and to come from Huelva, at the root.

*In PHI*: Compose > Rhythm Generator writes drum lanes, each a Euclidean rhythm (or a written
pattern, or a Xenakis sieve as a mask) of its own length, with a chance for each hit, humanized
loudness and timing, and rules: a fill every few bars, a rising or falling density, skipped
hits and lanes that turn a step. One seed draws every chance, so the same settings write the
same groove.

- [Euclidean rhythm](https://en.wikipedia.org/wiki/Euclidean_rhythm),
  [Tresillo (rhythm)](https://en.wikipedia.org/wiki/Tresillo_(rhythm)) and
  [Polyrhythm](https://en.wikipedia.org/wiki/Polyrhythm), Wikipedia.
- Viggo Brun, "Euclidean Algorithms and Musical Theory", *L'Enseignement Mathématique* 10 (1964),
  [doi:10.5169/seals-39413](https://doi.org/10.5169/seals-39413).
- Erv Wilson, "Viggo Brun's algorithm applied" (worksheets, 1997),
  [anaphoria.com](https://anaphoria.com/viggo2.pdf), and "A Sequence of Constant Structures as
  Predicted by Brun's Algorithm" (1995), [anaphoria.com](https://anaphoria.com/viggo3.pdf); see also
  Kraig Grady, ["Applying Viggo
  Brun's Algorithm to Rhythm"](https://anaphoria.com/ViggoRhythm.pdf).
- Benjamin Wardhaugh, ["Music and Euclid's algorithm"](https://plus.maths.org/os/issue40/features/wardhaugh/index),
  *Plus Magazine* (University of Cambridge, 2006).
- Godfried Toussaint, "The Euclidean Algorithm Generates Traditional Musical Rhythms",
  *Proceedings of BRIDGES* (Banff, 2005), pp. 47–56
  ([PDF](https://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf); extended version,
  [PDF](https://cgm.cs.mcgill.ca/~godfried/publications/banff-extended.pdf)); "The rhythm that conquered
  the world: What makes a 'good' rhythm good", *Percussive Notes* (November 2011), pp. 52–59;
  "Classification and phylogenetic analysis of African ternary rhythm timelines", *BRIDGES*
  (Granada, 2003); with J.-M. Díaz-Báñez et al., "El compás flamenco: a phylogenetic analysis",
  *BRIDGES* (Winfield, 2004), pp. 61–70; and *The Geometry of Musical Rhythm* (CRC Press, 2013;
  2nd ed. 2019). His publications on rhythm are listed on his
  [Comparative Musicology page](https://cgm.cs.mcgill.ca/~godfried/rhythm-and-mathematics.html).
