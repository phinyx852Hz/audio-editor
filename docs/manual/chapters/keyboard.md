# Working from the Keyboard

[]{.ix t="Keyboard-first design"}Everything PHI does is a command. Each command has a name, a place in a menu and usually a shortcut, and all of them can be found, run and rebound from the keyboard. This chapter shows the handful of keys that open up the rest.

## The command palette

[]{.ix t="Command palette"}Press `Ctrl+Shift+P` or `F1`. A search field opens over the window. Type a few letters of what you want ("norm", "fade out", "binaural") and the list narrows to the commands that match, each with its shortcut. Move with the arrows and press `Enter` to run one; `Esc` closes the palette.

![The command palette, finding every command with "fade" in its name.](images/command-palette.png)

The palette is also the quickest way to learn shortcuts: the key for each command is shown beside it.

## Menus from the keyboard

[]{.ix t="Menus!from the keyboard"}`F10` opens the first menu; the arrows move through the menus and their items, and `Enter` runs one. `Alt` with a menu's underlined letter opens that menu directly:

| Keys | Menu | Keys | Menu |
|---|---|---|---|
| `Alt+F` | File | `Alt+S` | Master |
| `Alt+E` | Edit | `Alt+T` | Transport |
| `Alt+P` | Process | `Alt+V` | View |
| `Alt+R` | Repair | `Alt+Z` | Visualizer |
| `Alt+C` | Effect (pressed again: Performance) | `Alt+X` | Mixer |
| `Alt+I` | Spatial | `Alt+A` | Sample |
| `Alt+W` | Warp | `Alt+Y` | Synth |
| `Alt+M` | Markers | `Alt+K` | Tracker |
| `Alt+B` | Album | `Alt+O` | Options |
| `Alt+N` | Analyze | `Alt+H` | Help |
| `Alt+L` | Tools | | |

![The menus open from the keyboard with F10.](images/menu-f10.png)

## Changing shortcuts {#change-shortcuts}

[]{.ix t="Shortcuts!changing"}[]{.ix t="Keyboard Shortcuts window"}Options > Keyboard Shortcuts (`Ctrl+Shift+K`) lists every command with its keys. Type to filter the list, select a command and press the new key combination. If the keys are already in use, PHI says by which command, so nothing is lost by accident. Your changes are saved to `~/.config/phi/keymap.ini`; only the keys you changed are stored, so new commands in later versions keep their defaults.

![Keyboard Shortcuts, with conflict detection.](images/keyboard-shortcuts.png)

[Keyboard Shortcuts](#shortcuts) at the end of this manual lists every default.

## Hyprland and Omarchy {#hyprland-keys}

[]{.ix t="Hyprland!key bindings"}A key your desktop has bound never reaches a program. On Hyprland, PHI reads the desktop's own key bindings (`hyprctl binds`) when it starts and warns you if any of its shortcuts is taken, so you can change one or the other. PHI never uses the Super key on its own, which Omarchy keeps for itself.

Two keys are shared with the desktop on purpose: `Super+W` closes whatever has the keyboard in PHI, and `Super+F` takes the window with the keyboard full screen. They reach PHI only if Hyprland does not take them first; `Ctrl+W` always works.

## Moving between panels {#panel-focus}

[]{.ix t="Panels!moving between"}[]{.ix t="F6"}

| Keys | Does |
|---|---|
| `F6`, or `Alt` tapped on its own | Next panel |
| `Shift+F6` | Previous panel |
| `Alt` or `F6` held, with an arrow key | The panel on that side, as between windows on a tiling desktop |
| `Ctrl+F6` | Straight to the waveform |
| A panel's own key (such as `Ctrl+Shift+L` for the meters) | Opens that panel and gives it the keyboard |
| `Ctrl+W` or `Super+W` | Closes what has the keyboard: a panel, the mixer, a window of PHI's own, or the file when the editor has it |
| `Super+F` | The window with the keyboard full screen, and back |
| `Esc` | In a popped-out panel, back to the waveform |

`F6` goes round the waveform, Simplest, the Device Chain, the Master Section, the Meters, Markers, History, Diagnostics, References, Find and the Slicer. A closed panel opens while you are on it and closes again when you move on. With an arrow, closed panels stay closed.

## Choosing the audio device {#audio-device}

[]{.ix t="Audio device"}`Ctrl+,` (Options > Audio Settings) chooses where PHI plays: through ALSA directly, or through JACK, which on Arch and Omarchy is PipeWire's JACK support. The device, its sample rate and its block size show at the bottom right of the editor. A smaller block means less delay between pressing Play and hearing it, and more work for the processor; if the Audio meter in the corner turns red or the sound drops out, choose a larger one.

Surround files are heard folded down to stereo. Options > Play Surround on Separate Outputs sends each channel to its own output instead, when the device has enough of them.

::: tip
If PHI shows "No audio device", another program may hold the sound card through ALSA. Choose the JACK (PipeWire) output instead, which shares the card with every other program.
:::

## Five keys to learn first

| Key | Does |
|---|---|
| `F1` | Find any command |
| `Space` | Play and pause |
| `Ctrl+Z` | Undo |
| `F6` | Next panel |
| `Esc` | Back: clears a selection, closes a window, returns to the waveform |
