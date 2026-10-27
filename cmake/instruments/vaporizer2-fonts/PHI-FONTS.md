# Vaporizer2's fonts in PHI

PHI builds Vaporizer2 with free fonts in place of four of its eight, which may not be
redistributed (SF UI Display, Apple's, is licensed for mock-ups only; Trade Gothic LT is a
commercial Linotype typeface; Alte Haas Grotesk is freeware with no redistribution terms).
The file names inside the plugin stay the same.

| Skin | Vaporizer2's font | In PHI's build | Licence |
|---|---|---|---|
| Classic | Open Sans, by Steve Matteson (Google) | Open Sans, unchanged | Apache-2.0 (`LICENSE-OpenSans.txt`) |
| Tech | SF UI Display | Inter, by Rasmus Andersson and the Inter Project Authors | OFL-1.1 (`OFL-Inter.txt`) |
| Dark | Trade Gothic LT | Archivo Narrow, by Omnibus-Type and the Archivo Narrow Project Authors | OFL-1.1 (`OFL-ArchivoNarrow.txt`) |
| IceCubes | Alte Haas Grotesk | Schibsted Grotesk, by the Schibsted-Grotesk Project Authors | OFL-1.1 (`OFL-SchibstedGrotesk.txt`) |

The OFL fonts are static Regular and Bold instances of the variable fonts in Google Fonts,
made with fontTools (cmake/instruments/vaporizer2-fonts in PHI's repository).
