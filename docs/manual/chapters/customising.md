# Making PHI Yours

[]{.ix t="Customising"}PHI can look the way you like and fit the way you work: themes, your own waveform and meter colours, a see-through window, keys of your choosing, and a few settings kept in plain text files.

## Themes {#themes}

[]{.ix t="Themes"}PHI comes with **PHI Dark** and **PHI Light**. Options > Theme (`Ctrl+Shift+T`) lists them with any of your own. Moving through the list previews each theme at once; `Enter` keeps it and `Esc` goes back. **Reload** picks up files you have just added or changed, and a file with a mistake in it is listed with the reason it was skipped.

![Choosing a theme: the Golden theme from the end of this section, previewed over the spectral view.](images/theme-picker.png)

### Making a theme

[]{.ix t="Themes!making"}A theme is a small JSON file in `~/.local/share/phi/themes`. It starts from one of the built-in themes and lists only the colours it changes:

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

- **name** (required) is shown in the list and must be unique.
- **base** is `"dark"` (the default) or `"light"`.
- **colors** are written `#RGB`, `#RRGGBB` or `#RRGGBBAA` (alpha last, as in CSS), or as colour names such as `"orange"`.
- **spectrogram** is two or more colours, from silence (−120 dBFS) to full scale (0 dBFS).

| Key | Colours |
|---|---|
| `window`, `windowText` | Window and panel backgrounds and their text |
| `base`, `alternateBase`, `text`, `placeholderText` | Lists, text fields and hint text |
| `button`, `buttonText` | Buttons and menus |
| `highlight`, `highlightedText` | Selected items in lists and menus |
| `accent` | The keyboard focus outline and links |
| `mid` | Separators |
| `disabledText` | Greyed-out commands |
| `waveform`, `waveformBackground`, `centerLine` | The waveform |
| `selection` | The selected range (use alpha) |
| `cursor` | The edit and play cursors |
| `marker`, `region` | Marker and region lines and names |
| `meterBackground`, `meterLow`, `meterMid`, `meterHigh`, `meterPeakHold` | Level meters: below −18 dB, −18 to −6 dB, above −6 dB |
| `scope` | The goniometer, polar and Lissajous traces |
| `levelQuiet` … `levelPeak` | The waveform coloured by level, at −48, −24, −12, −6, −3 and 0 dBFS |

::: note
Themes change colours only, for now. Fonts, icons and widget images are planned.
:::

::: phi
**A golden theme.** Here is the theme in the picture above, in the colours of this manual and of the Φ mark. Save it as `golden.json` in the themes folder and press Reload:

```json
{
  "name": "Golden",
  "base": "dark",
  "colors": {
    "window": "#0b0b0c", "base": "#111113", "alternateBase": "#161618",
    "button": "#161618", "accent": "#c8a24a",
    "highlight": "#c8a24a", "highlightedText": "#0b0b0c",
    "waveform": "#c8a24a", "waveformBackground": "#0b0b0c",
    "selection": "#c8a24a40", "marker": "#e8d9a8", "scope": "#c8a24a"
  },
  "spectrogram": ["#000000", "#1a1408", "#5a4518", "#c8a24a", "#fff6dc"]
}
```
:::

## Waveform and meter colours {#waveform-colours}

[]{.ix t="Waveform and Meter Colors"}Options > Waveform and Meter Colors (`Ctrl+Shift+W`) sets your own colours on top of any theme: the waveform, its background, the selection, the cursor, the centre line, markers and regions, the imaging display's trace, and the meters (low, middle, high and peak hold). The colour picker is part of the same window and every change shows at once. **Theme default** puts a colour back.

![Waveform and Meter Colors, with its colour picker.](images/waveform-colours.png)

[]{.ix t="Colour Waveform by Level"}**Colour by level.** View > Colour Waveform by Level (`Ctrl+Alt+Shift+W`) colours the waveform by how loud it is, from blue when quiet through cyan, green, yellow and orange to red at 0 dBFS, so loud passages and clipping can be seen from across the room. The six colours are in the same window, and in themes.

## A see-through window {#opacity}

[]{.ix t="Window opacity"}The **Opacity** slider at the bottom right of the window (or View > Window Opacity) makes the whole PHI window see-through; `Ctrl+Alt+-` and `Ctrl+Alt+=` change it from the keyboard. Your compositor's own opacity, such as a Hyprland window rule, applies on top.

With the Visualizer behind PHI (`Ctrl+F11`), the same slider sets how much of it glows through. See [The Visualizer and Sonify](#visualizer).

## Focus follows the mouse {#focus-follows-mouse}

[]{.ix t="Focus Follows Mouse"}View > Focus Follows Mouse is on from the start, like Hyprland: point at the waveform, the Device Chain or a panel and your keys go there, without a click. It never pulls focus out of a text field or dialog. Turn it off to have keys stay where you last clicked.

## Keys

Every command can have keys of your choosing in Options > Keyboard Shortcuts (`Ctrl+Shift+K`). See [Changing shortcuts](#change-shortcuts), and the full list in [Keyboard Shortcuts](#shortcuts).

## Your audio interface {#interface-mixer}

[]{.ix t="Audio Interface Mixer"}Options > Audio Interface Mixer (`Ctrl+Alt+Shift+I`) opens your interface's own routing and mixer program, or brings it to the front on Hyprland. It is alsa-scarlett-gui to begin with, for Focusrite Scarlett and Clarett interfaces; if it is missing, PHI says how to install it (`yay -S alsa-scarlett-gui`). Choose another program with Options > Audio Interface Mixer Program.

## Settings files {#settings-files}

[]{.ix t="settings.ini"}Settings are kept in `~/.config/phi/settings.ini` and your keys in `~/.config/phi/keymap.ini`, both plain text. Most settings have a place in PHI's windows; a few are only in the file, such as how often unsaved files are copied for recovery (`autosaveSeconds` under `[recovery]`, in seconds; 0 turns it off). Quit PHI before changing the file by hand. Every folder PHI uses is listed in [Files and Folders](#files-folders).

## Starting afresh {#reset-configuration}

[]{.ix t="Reset Configuration"}Tools > Reset Configuration puts the window layout, theme, colours and remembered values back as they were at first, and can reset the shortcuts and the plug-in list too. PHI then restarts. Your files, macros and Master Section are kept.

To put only the panels back, use View > Reset Panel Layout (`Ctrl+Alt+0`).
