# Markers and Regions

[]{.ix t="Markers"}[]{.ix t="Regions"}A marker names a moment in a file: a verse, a cough, a downbeat. A region names a stretch. Both are saved inside WAV files as cue points and regions, so other programs see them too.

## Adding markers

| Keys | Does |
|---|---|
| `M` | Adds a marker at the cursor |
| `Shift+M` | Makes a region from the selection |
| `'` | Goes to the next marker |
| `;` | Goes to the previous marker |
| Double-click between markers | Selects everything between them |

Many other tools add markers for you: Mark Beats and Mark Onsets (see [Analysis with Vamp plugins](#vamp)), Find Similar Sounds and the Diagnostics panel (see [Repair and Restoration](#repair)), and Nyquist's labelling effects.

## The Markers panel

[]{.ix t="Markers panel"}View > Markers (`Ctrl+Shift+M`) lists every marker and region with its start, end and length.

![Four markers over the waveform, and the Markers panel listing them below.](images/markers-panel.png)

| Key | Does |
|---|---|
| `Enter` | Goes to the marker |
| `F2` | Renames it |
| `Del` | Removes it |
| `Esc` | Back to the waveform |

## Exporting at markers

[]{.ix t="Export Slices at Markers"}[]{.ix t="Labels"}

- **Markers > Export Slices at Markers** writes each piece between markers to its own numbered file, ready for a sampler.
- **Markers > Export Markers as Labels** writes the markers as Audacity labels, Sonic Visualiser labels or a CSV file.
- [Export](#export) can write each region, or each part between markers, as its own file in any format.
