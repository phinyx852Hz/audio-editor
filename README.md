# PHINYX

A keyboard-first audio editor and mastering suite for Linux, combining the best of
Adobe Audition, Sound Forge and WaveLab. Optimized for Arch Linux, Omarchy and Hyprland.

Status: early foundation (Phase 0). It is not a usable editor yet. See the full plan in [docs/plan.md](docs/plan.md).

Built with a Qt 6 interface (native Wayland) on top of a JUCE 8 audio engine.

## What works now
- Open a WAV/AIFF/FLAC/Ogg/MP3 file (`Ctrl+O`, or pass it on the command line) and see its waveform
- Playback through ALSA or JACK/PipeWire (`Space`), cursor movement (`Left`/`Right`, `Ctrl+Left`/`Ctrl+Right`, `J`/`L`, `Home`/`End`), zoom (`+`/`-`, `Ctrl+0`)
- Every action is a command with a keybinding:
  - `Ctrl+Shift+P` or `F1`: command palette, searches every command and shows its shortcut
  - `F10` or `Alt+F/T/V/O/H`: open menus, then arrow keys
  - `Ctrl+Shift+K`: change any key binding, with conflict detection; saved to `~/.config/phinyx/keymap.ini`
  - `Ctrl+,`: choose the audio output device

## Build and run (Arch Linux)
```sh
sudo pacman -S --needed base-devel git cmake ninja qt6-base qt6-wayland alsa-lib jack2
git clone https://github.com/phinyx852Hz/audio-editor
cd audio-editor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/phinyx_artefacts/Release/phinyx
```
JUCE 8.0.15 is downloaded automatically during the first configure; pass `-DJUCE_DIR=/path/to/JUCE` to use a local checkout.

To install system wide instead, use the PKGBUILD: `cd packaging/arch && makepkg -si`.

## Hyprland
PHINYX runs natively on Wayland. Its window class is `phinyx`, so you can target it in `hyprland.conf`, e.g.
`windowrulev2 = workspace 3, class:^(phinyx)$`.

## License
GNU Affero General Public License v3.0 only. See `LICENSE`. JUCE is used under its AGPLv3 license; Qt under the LGPLv3.
