# PHI themes

PHI ships with **PHI Gold** (the default, gold on near-black like the PHI manual), **PHI Dark** and **PHI Light**. Anyone can make more themes. A theme is a small JSON file
placed in

```
~/.local/share/phi/themes/
```

(or `$XDG_DATA_HOME/phi/themes/`). Pick it with **Options > Theme** (`Ctrl+Shift+T`). Moving through the list
previews each theme live, `Enter` keeps it and `Esc` goes back. **Reload** picks up files you just added or edited, and
files with mistakes are listed with the reason they were skipped.

## Theme Generator

**Tools > Theme Generator** makes a whole theme from a few choices, so you don't have to pick 32 colours by hand. The
whole app shows the theme as you work; **Cancel** goes back to the theme you had, **Save Theme** puts it in the themes
folder and uses it (Options > Theme lists it from then on), and **Export** writes the JSON anywhere, to share it.

Start from any of these:

- **Main Colour**: a colour picker or a `#rrggbb` value. The main colour is used for the waveform, highlights and selections.
- **From Picture** or **From Wallpaper**: PHI finds the main colours of the picture and shows them as swatches; it
  starts from the most striking common one, dark or light to suit the picture, and clicking a swatch uses that one instead.
- **From a theme**: any built-in or installed theme. A theme made with the generator opens with the settings it was
  made with, and colours you had picked by hand are kept.
- **From REAPER Theme**: the colours of a `.ReaperTheme` file, or the one inside a `.ReaperThemeZip` (see below).
- **Describe it**: a few words such as "a 1970s tape deck, warm and brown", turned into settings by a language model
  (optional and off until you set it up, see below).

Then shape it:

- **Mode**: dark or light.
- **Harmony**: how the accent, marker and region colours relate to the main colour: one colour, neighbours
  (analogous), opposite (complementary), split opposite, or three-way (triadic). **Shuffle** tries other accents
  that go with the same main colour.
- **Saturation**, **Contrast** and **Background tint** (how much the main colour tints the panels).
- **Meters**: classic green, yellow and red, or the theme's own colours. The top of the meter and the peak level
  colour stay red either way.
- **Colours**: every colour of the theme. Double-click one to choose it yourself; it is then ticked **Keep**, and
  kept colours stay as they are while you change anything else. Untick Keep to let the generator choose it again.

The colours are worked out in OKLCH, where equal steps in lightness look equal, so a theme stays balanced whatever
main colour you start from.

**Contrast checks** measure each kind of text and line against its background with the WCAG 2 contrast ratio:
4.5 for text, 3 for the waveform, cursor, markers, regions and meters. With **Keep text readable** on (the default),
the generator moves the lightness of any colour that falls short until it passes. A colour you keep is not moved,
so a check can still fail when you choose both colours of a pair yourself; the list shows which.

Your own colours from Options > Waveform and Meter Colors still apply on top of whatever theme you use.

### Describe it (AI, optional)

A language model can turn a description into generator settings. It is off until you turn it on in **AI Settings**,
and PHI works the same without it. The model only chooses the settings (main colour, dark or light, harmony,
saturation, contrast, tint, meters) and, where the description calls for one, particular colours; the generator
builds the theme from them and the contrast checks still apply, so a poor guess still gives a readable theme.

- **A model on this computer** (the default): any server with an OpenAI-style `/chat/completions` endpoint, such as
  [Ollama](https://ollama.com) (`http://localhost:11434/v1`), LM Studio (`http://localhost:1234/v1`) or the
  llama.cpp server (`http://localhost:8080/v1`). Nothing leaves your computer and no internet connection is needed:
  the model already knows about colour from its training. With Ollama, `ollama pull llama3.2` and it is ready.
- **Claude, in the cloud**: sends your description to Anthropic's API and needs an API key from
  console.anthropic.com (or `ANTHROPIC_API_KEY` set in the environment). Each request costs a little.

The address, model name and key are kept in `~/.config/phi/settings.ini` under `[themeAi]`.

### REAPER themes

A REAPER colour theme is a text file of named colours, so the generator can take its colours: backgrounds, text,
buttons, the waveform (peaks) and its background, the edit cursor, grid lines, markers, regions, the selection and
the meters (`col_main_bg2`, `col_main_text2`, `col_main_editbk`, `col_buttonbg`, `col_tr1_peaks`, `col_mi_bg2`,
`col_cursor`, `col_gridlines`, `marker`, `region`, `col_tl_bgsel`, `col_vubot`, `col_vumid`, `col_vutop` and a few
more). Those colours are kept and the rest are generated to match. A `.ReaperThemeZip` also holds images and WALTER
layout scripts; they draw REAPER's own track panels and mixer strips, which PHI doesn't have, so they are left out.
REAPER themes vary in how they use their colours, so check the result and change what doesn't suit PHI.

### Scripts

The generator is plain C++ in `Source/core/ThemeGen.h` (no Qt): settings in, colours out, plus the palette of a
picture, the contrast checks and the REAPER import. Tools > Scripts can offer the same functions, so a script could
make or adjust themes, and a theme file keeps the settings it was made with under `"generator"`:

```json
"generator": { "seed": "#c8a24a", "harmony": "complementary", "saturation": 1.00, "contrast": 0.50,
               "tint": 0.50, "meters": "classic", "variation": 0 }
```

Options > Theme ignores that part.

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
- `base`: the built-in theme to start from, `"dark"`/`"PHI Dark"` (default), `"light"`/`"PHI Light"`
  or `"gold"`/`"PHI Gold"`.
  You only list the colours you change.
- `colors`: any of the keys below. Values are `#RGB`, `#RRGGBB`, `#RRGGBBAA` (alpha last, as in CSS) or SVG
  colour names such as `"orange"`.
- `spectrogram`: two or more colours, from silence (-120 dBFS) to full scale (0 dBFS).
- `dark`: `true` or `false`, a hint for future icon sets. It is inherited from `base` when omitted.
- `generator`: the Theme Generator's settings, so it can open the theme as it was made. Optional.

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
