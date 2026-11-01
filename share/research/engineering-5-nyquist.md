<!--
Research > Engineering > Nyquist: the language behind Process > Effect, the Nyquist entries of
the Analyze and Tools menus, Tools > Nyquist Prompt, and the plug-ins folder.
-->

# Engineering / Nyquist

## Nyquist

A programming language for sound and music: a dialect of Lisp in which a sound is a value like a
number, so that a whole process can be written as one expression. `(mult *track* 0.5)` halves the
level of the selection; `(lp *track* 1000)` passes it through a low-pass filter at 1 kHz. Sounds
are computed only as they are needed, a block at a time (lazy evaluation), so a few lines can work
on hours of audio. Behaviours such as `stretch` and `at` change the time of everything inside
them, which makes it as much a language for composing as for processing.

PHI runs Nyquist with libnyquist, the same library Audacity uses, so Audacity's plug-ins and the
many shared online work in PHI unchanged. The language is named after
[Harry Nyquist](research:Sample%20rate%20and%20the%20Nyquist%20frequency).

Roger B. Dannenberg wrote it at Carnegie Mellon University from the early 1990s, on David Betz's
XLISP interpreter, after his earlier languages Arctic and Canon; a second syntax, **SAL**, came
later for those who prefer it to Lisp's parentheses. Dannenberg and his student Dominic Mazzoni
started Audacity at Carnegie Mellon in 1999, and Nyquist has been Audacity's plug-in language
since its early versions.

- [Nyquist (programming language)](https://en.wikipedia.org/wiki/Nyquist_(programming_language)),
  Wikipedia.
- [Lisp (programming language)](https://en.wikipedia.org/wiki/Lisp_(programming_language)),
  Wikipedia.
- [XLISP](https://en.wikipedia.org/wiki/XLISP), Wikipedia.
- [Audacity (audio editor)](https://en.wikipedia.org/wiki/Audacity_(audio_editor)), Wikipedia.
- Roger B. Dannenberg, "The Implementation of Nyquist, a Sound Synthesis Language", *Computer
  Music Journal* 21 (1997).
- Roger B. Dannenberg, [*Nyquist Reference Manual*](https://www.cs.cmu.edu/~rbd/doc/nyquist/).

## Nyquist plug-ins

A plug-in is a text file ending in .ny: a header of lines beginning with `$` (or `;` in older
files), then the code. The header names the effect (`$name`), says where it goes (`$type`), and
describes its controls; PHI reads it and builds the dialog, as Audacity does:

- **process**: changes the selection; PHI lists it in Process > Effect.
- **generate**: makes new sound in place of the selection; Process > Effect > Generate.
- **analyze**: measures and answers with labels or text; the Analyze menu (Label Sounds, Measure
  RMS).
- **tool**: does something else, with or without audio; the Tools menu (Regular Interval Labels,
  Sample Data Export and Import, the Nyquist Plugin Installer).
- **spectral**: works on a selection of time and frequency (Spectral Delete, Spectral Edit
  Shelves).

The controls (`$control`) are sliders of real or whole numbers, number boxes, times, text, choices
and file choosers, each setting a Lisp variable the code reads. The selection reaches the code as
`*track*`, with its sample rate, length and frequency band in variables beside it.

Tools > Open Nyquist Plug-ins Folder opens the folder for your own; Rescan Nyquist Plug-ins reads
it again.

- [Plug-in (computing)](https://en.wikipedia.org/wiki/Plug-in_(computing)), Wikipedia.
- [Nyquist (programming language)](https://en.wikipedia.org/wiki/Nyquist_(programming_language)),
  Wikipedia.
- Audacity, [Nyquist Plug-ins Reference](https://manual.audacityteam.org/man/nyquist.html).

## The Nyquist Prompt

Tools > Nyquist Prompt (Ctrl+Alt+Y) runs a piece of Nyquist typed in on the spot, on the
selection, without making a plug-in file: the quickest way to try an idea, to do a one-off
calculation on the audio, or to learn the language. It guesses whether the code is Lisp or SAL,
runs it as a version 4 plug-in, and shows what it printed, a returned number or text, or the
error. A header pasted in with it gives it controls, as a plug-in has.

A prompt that evaluates what is typed and prints the result is a read-eval-print loop (REPL), the
way Lisp has been used interactively since the 1960s.

- [Read–eval–print loop](https://en.wikipedia.org/wiki/Read%E2%80%93eval%E2%80%93print_loop),
  Wikipedia.

## Label Sounds and Measure RMS

The two Nyquist entries of the Analyze menu. **Label Sounds** finds the sounds between silences
and marks each with a label: a threshold (peak or RMS), the shortest silence that separates two
sounds, the shortest time between labels, where the label goes (before, after or around each
sound) and the label text. It is how a recording of many takes, samples or spoken items is split
quickly. **Measure RMS** reports the RMS level of the selection in dB.

- [Root mean square](https://en.wikipedia.org/wiki/Root_mean_square), Wikipedia.
- [Voice activity detection](https://en.wikipedia.org/wiki/Voice_activity_detection), Wikipedia.

## Nyquist tools

The Nyquist entries of the Tools menu. **Regular Interval Labels** places labels at equal times or
in equal numbers across the selection, for cutting a long file into pieces. **Sample Data Export**
writes the sample values to a text or CSV file, to study them in a spreadsheet or another program;
**Sample Data Import** reads such a file back as sound. The **Nyquist Plugin Installer** copies .ny
files into the plug-ins folder.

- [Comma-separated values](https://en.wikipedia.org/wiki/Comma-separated_values), Wikipedia.
- [Pulse-code modulation](https://en.wikipedia.org/wiki/Pulse-code_modulation), Wikipedia.
