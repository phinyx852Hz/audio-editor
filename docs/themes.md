# PHI themes

PHI ships with **PHI Dark** and **PHI Light**. Anyone can make more themes. A theme is a small JSON file
placed in

```
~/.local/share/phi/themes/
```

(or `$XDG_DATA_HOME/phi/themes/`). Pick it with **Options > Theme** (`Ctrl+Shift+T`). Moving through the list
previews each theme live, `Enter` keeps it and `Esc` goes back. **Reload** picks up files you just added or edited, and
files with mistakes are listed with the reason they were skipped.

## Format

```json
{
  "name": "Midnight Teal",
  "base": "dark",
  "colors": {
    "accent": "#ff9e64",
    "waveform": "#2ac3de",
    "selection": "#2ac3de40",
    "meterLow": "#9ece6a"
  },
  "spectrogram": ["#000000", "#1a1b26", "#3d59a1", "#2ac3de", "#ffffff"]
}
```

- `name` (required): shown in the theme list. Must be unique.
- `base`: the built-in theme to start from, `"dark"`/`"PHI Dark"` (default) or `"light"`/`"PHI Light"`.
  You only list the colours you change.
- `colors`: any of the keys below. Values are `#RGB`, `#RRGGBB`, `#RRGGBBAA` (alpha last, as in CSS) or SVG
  colour names such as `"orange"`.
- `spectrogram`: two or more colours, from silence (-120 dBFS) to full scale (0 dBFS).
- `dark`: `true` or `false`, a hint for future icon sets. It is inherited from `base` when omitted.

| Key | Used for |
|---|---|
| `window`, `windowText` | Window and panel backgrounds and their text |
| `base`, `alternateBase`, `text`, `placeholderText` | Lists, text fields, hint text |
| `button`, `buttonText` | Buttons and menus |
| `highlight`, `highlightedText` | Selected items in lists and menus |
| `accent` | Keyboard focus outline and links |
| `mid` | Separators |
| `disabledText` | Greyed out commands |
| `waveform`, `waveformBackground`, `centerLine` | Waveform display |
| `selection` | Selected range overlay (use alpha) |
| `cursor` | Edit and playback cursor |
| `marker`, `region` | Marker and region lines and names |
| `meterBackground`, `meterLow`, `meterMid`, `meterHigh`, `meterPeakHold` | Level meters (below -18 dB, -18 to -6 dB, above -6 dB) |
| `scope` | Imaging display: goniometer, polar and Lissajous traces (defaults to the theme's `waveform` colour) |
| `levelQuiet`, `levelSoft`, `levelMedium`, `levelLoud`, `levelHot`, `levelPeak` | Waveform coloured by level (View > Colour Waveform by Level): the colours at -48, -24, -12, -6, -3 and 0 dBFS, blended in between |

Themes are colour-only for now. Fonts, icons and widget images are planned (plan section 2.8b).
