# PHINYX

A keyboard-first audio editor and mastering suite for Linux, combining the best of
Adobe Audition, Sound Forge and WaveLab. Optimized for Arch Linux, Omarchy and Hyprland.

Status: Phase 1 in progress. Basic editing works; markers, meters, spectrogram and the Master Section come next. See the full plan in [docs/plan.md](docs/plan.md).

Built with a Qt 6 interface (native Wayland) on top of a JUCE 8 audio engine.

## What works now
- Open WAV/AIFF/FLAC/Ogg/MP3 (`Ctrl+O`, or pass a file on the command line); save as WAV/FLAC/AIFF/Ogg (`Ctrl+S`, `Ctrl+Shift+S`) with bit depth and TPDF dither. WAV markers (cue points and regions) are kept.
- Playback through ALSA or JACK/PipeWire (`Space`), cursor movement (`Left`/`Right`, `Ctrl+Left`/`Ctrl+Right`, `J`/`L`, `Home`/`End`), zoom (`+`/`-`, `Ctrl+0`, `Ctrl+E` to the selection, `Ctrl`+mouse wheel)
- Selection by mouse drag, `Shift`+click, `Shift`+arrows/`J`/`L`/`Home`/`End`, `Ctrl+A`; `Esc` clears it
- Editing: cut/copy/paste/delete (`Ctrl+X`/`C`/`V`, `Del`), trim to selection (`Ctrl+T`), insert silence (`Ctrl+Alt+S`)
- Processing (on the selection, or the whole file when nothing is selected): gain (`Ctrl+G`), normalize to peak dBFS or integrated LUFS (`Ctrl+Shift+N`), fade in/out with four curve shapes (`Ctrl+Shift+I`/`O`), reverse (`Ctrl+Shift+R`), invert (`Ctrl+Alt+I`), DC offset removal (`Ctrl+Alt+D`)
- Unlimited undo/redo (`Ctrl+Z`, `Ctrl+Shift+Z`) and a History panel (`Ctrl+H`, `Enter` jumps to a step, `Esc` returns to the waveform); you are asked before unsaved changes are lost
- Every action is a command with a keybinding:
  - `Ctrl+Shift+P` or `F1`: command palette, searches every command and shows its shortcut
  - `F10` or `Alt+F/E/P/T/V/O/H`: open menus, then arrow keys
  - `Ctrl+Shift+K`: change any key binding, with conflict detection; saved to `~/.config/phinyx/keymap.ini`
  - `Ctrl+,`: choose the audio output device

## Build and run (Arch Linux)
```sh
sudo pacman -S --needed base-devel git cmake ninja qt6-base qt6-wayland alsa-lib jack2 \
  freetype2 fontconfig libx11 libxrandr libxinerama libxcursor libxcomposite libxext mesa
git clone https://github.com/phinyx852Hz/audio-editor
cd audio-editor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/phinyx_artefacts/Release/phinyx
```
Run the core tests with `ctest --test-dir build`.
JUCE 8.0.15 is downloaded automatically during the first configure (the freetype/X11 packages above are only needed to build JUCE's small `juceaide` helper); pass `-DJUCE_DIR=/path/to/JUCE` to use a local checkout.

To install system wide instead, use the PKGBUILD: `cd packaging/arch && makepkg -si`.

## Hyprland
PHINYX runs natively on Wayland. Its window class is `phinyx`, so you can target it in `hyprland.conf`, e.g.
`windowrulev2 = workspace 3, class:^(phinyx)$`.

## License
GNU Affero General Public License v3.0 only. See `LICENSE`. JUCE is used under its AGPLv3 license; Qt under the LGPLv3.
