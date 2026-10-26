# Stochasticize

The owner asked on 2026-10-26 for Xenakis's stochastic methods in the MIDI clip's generative
tools, written as our own C++ rather than embedded Python, and spelled "stochasticize". Every
name in the dialog is lower case (owner).

**stochasticize...** is in the mixer's MIDI Clip section, beside **counterpoint...**, and in the
**Mode** menu. It draws a new line from the clip shown in the Device Chain and puts it on a new
track beside the clip, or in place of the clip (Ctrl+Z puts the clip back). The line is drawn
once, when you press **write**: even a dense cloud of thousands of notes takes a few
milliseconds, and it plays like any other MIDI clip.

## The methods

- **st cloud (achorripsis, st/10)**, from *Formalized Music*, chapters I and V. The line is cut
  into sections (**each section**, in beats). Each section draws its own density from the
  exponential law about the mean (**density**, in notes a beat), and its notes start as a
  Poisson process. Each pitch leaps from the last by the law of intervals (**leaps**). Xenakis's
  linear law, f(j) = 2/a (1 − j/a), makes small leaps likeliest. Each note's length and loudness
  come from laws of their own.
- **markov screens (analogique a and b)**, from *Formalized Music*, chapter III. Each section is a
  screen with three states: a low or high register, soft or loud, and sparse or dense. A 2×2
  Markov chain for each state picks the next screen. **screens stay** is the chance that a state
  stays the same from one screen to the next. Analogique's matrices move more often than they
  stay, so 0.2 is the default. A cloud fills each screen. The status line lists the screens drawn:
  `l`/`h` for register, `p`/`f` for loudness, and `s`/`d` for density.

The laws available for leaps, lengths and loudness are uniform, linear, exponential, gaussian,
cauchy, logistic and arcsine. These are the laws *Formalized Music* uses. Linear, exponential,
cauchy and logistic favour values near the centre. Arcsine favours the ends.

## Sieves

A sieve is Xenakis's way of writing a scale or a rhythm as arithmetic ("Sieves", *Perspectives of
New Music* 28/1, 1990). `m@r` is every whole number that is r more than a multiple of m. You join
them with `|` (either), `&` (both) and `-` (not), and group them with brackets. `&` binds tighter
than `|`.

- **pitches**: a sieve counted in semitones from the **root**. Empty means every key in the
  **range**. *Fill from* writes in the mode (the clip's tuning, or the Mode menu's mode), the
  clip's own notes, every key, or one of Xenakis's or Messiaen's sieves. Xenakis's major scale
  is `(-3@2 & 4@0) | (-3@1 & 4@1) | (3@2 & 4@2) | (-3@0 & 4@3)`, and the same scale written back
  by his analysis is `6@5 | 12@0 | 12@2 | 12@4 | 12@7 | 12@9`.
- **rhythm**: free (a Poisson process), or **on the rhythm sieve**. The sieve counts steps from
  the clip's start, at **steps a beat**. Each step in the sieve is taken with the chance the
  density gives it. *Fill from* writes in the clip's own onsets, a sieve of Xenakis's (Psappha),
  or any of the Mode menu's rhythmic modes. For example, the son clave 3-2 becomes
  `16@0 | 16@3 | 16@6 | 16@10 | 16@12`.

A filled sieve is yours to edit. The dialog says when one does not read, and **write** waits until
it does.

## Settings and code

- **variation**: another number draws another line from the same laws. The same number and
  settings draw the same line on every computer, because the random source is our own.
- The dialog's last choices are kept in settings.ini under `stochastic/`.
- Code: `Source/mixer/Stochastic.{h,cpp}` (plain C++: the laws, sieves, clouds and screens) and
  `Source/ui/StochasticSection.cpp` (the dialog). The tests are `testStochastic` in
  `tests/CoreTests.cpp`. They check Xenakis's major scale, that every rhythmic mode round-trips
  through a sieve, that the laws are centred, that lines stay in their range, sieve and grid, and
  that a seed repeats.
- Everything here is our own code, written from Xenakis's texts. No other program's code was
  ported, so there are no licences to carry.

## Next

GENDYN (dynamic stochastic synthesis, *Formalized Music* chapter XIV and *GENDY3*) as a voice for
the PHI synth. Unlike the generator, it runs in real time, but it is light: a dozen breakpoints
moved by random walks each wave cycle.
