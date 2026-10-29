# Xen Tuner's tuning configs

The `tunings/` folder is Xen Tuner's library of tuning and notation configs, copied from
[euwbah/musescore-xen-tuner](https://github.com/euwbah/musescore-xen-tuner) at commit 936c5c6
(v0.3.6, 2026-01-15): HEJI, HEWM, FJS, Sagittal (Evo and Revo, about a hundred EDOs), ups and downs,
Kite, Fokker, Stein-Zimmermann, chromatic staves, NEJIs and the user folder. Only the `.txt`
configs and the READMEs were taken; `test/`, the generator scripts, the `.json` caches and the
MuseScore scores were left out, and so was `sagittal/311edo old.txt`, which names a symbol (`//Y`) Xen Tuner itself cannot read. Nothing was changed.

Xen Tuner is © 2023 euwbah and its contributors, licensed under the GNU General Public License,
version 3 or later (`LICENSE` here). PHI reads these files with its own port of Xen Tuner's config
parser (`Source/core/XenTuner.cpp`) for the MIDI editor's staff notation. The format is described
in Xen Tuner's README, under "How to: tuning configuration".
