# Nyquist Effects and Macros

## Nyquist effects {#nyquist}

[]{.ix t="Nyquist"}Nyquist is a small language for sound, by Roger B. Dannenberg, best known from Audacity. PHI runs it with the same library Audacity 3.7.9 uses, so Audacity's Nyquist effects work in PHI, and so do your own.

### The bundled effects

| Menu | Effects |
|---|---|
| Effect | Adjustable Fade, Delay, Legacy Limiter, Noise Gate, Notch Filter, Spectral Delete, Spectral Edit Shelves, Studio Fade Out, Tremolo |
| Effect > Generate | Pluck, Rhythm Track, Risset Drum |
| Analyze | Label Sounds, Measure RMS (labels become markers) |
| Tools | Nyquist Prompt, Nyquist Plugin Installer, Regular Interval Labels, Sample Data Export, Sample Data Import, Open Nyquist Plug-ins Folder, Rescan Nyquist Plug-ins |

Spectral Delete and Spectral Edit Shelves use the frequency band you selected in the spectral view.

Each effect opens a window built from its controls, which remembers your values. Every effect is a command, so it is in the command palette and can have a shortcut. **Repeat Last Effect** (`Ctrl+Shift+Y`) runs the last one again with the same settings.

### Your own effects

[]{.ix t="Nyquist!plug-ins folder"}Put `.ny` files in `~/.local/share/phi/plug-ins` (Tools > Open Nyquist Plug-ins Folder) and choose Tools > Rescan Nyquist Plug-ins. An installed Audacity's plug-ins (`/usr/share/audacity/plug-ins` and its user folders) are listed too.

### The Nyquist Prompt

[]{.ix t="Nyquist Prompt"}Tools > Nyquist Prompt (`Ctrl+Alt+Y`) runs Nyquist code you type, in Lisp or SAL, on the selection or the whole file. `Ctrl+Enter` runs it; **Debug** shows what it printed.

```lisp
; Turn the selection down by 6 dB
(mult *track* 0.5)
```

::: note
Preview and Audacity's scripting commands (`AUD-DO`) are not available yet.
:::

## Macros {#macros}

[]{.ix t="Macros"}A macro is a list of commands to run in one go, like Audacity's. PHI records it as you work.

::: steps
1. Tools > Record Macro (`Ctrl+F9`).
2. Use the commands you want, OK'ing their windows with the settings you want.
3. Press `Ctrl+F9` again and name the macro.
:::

**Apply Macro** (`F9`) runs one on the selection or the file, without its windows appearing.

**Macros** (`Shift+F9`) lists them to run, rename and delete, and to add, remove or reorder their steps. A step can be set to ask for its settings each time, for a macro that is nearly the same every time.

Each macro is a `.phmacro` file in `~/.config/phi/macros`. For processing many files at once, see the [Batch Processor](#batch).
