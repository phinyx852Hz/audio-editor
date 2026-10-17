# Audio Editor

A keyboard-first audio editor and mastering suite for Linux, combining the best of
Adobe Audition, Sound Forge and WaveLab. Optimized for Arch Linux, Omarchy and Hyprland.

Status: Phase 0 scaffold. See the full plan in [docs/plan.md](docs/plan.md).

## What works now
- Open a WAV/AIFF/FLAC/Ogg/MP3 file (`Ctrl+O`, or pass it on the command line) and see its waveform
- Playback through ALSA or JACK/PipeWire (`Space`), cursor movement (`Left`/`Right`, `Ctrl+Left`/`Ctrl+Right`, `J`/`L`, `Home`/`End`), zoom (`+`/`-`, `Ctrl+0`)
- Every action is a command with a keybinding:
  - `Ctrl+Shift+P` or `F1`: command palette, searches every command and shows its shortcut
  - `F10` or `Alt+F/T/V/O/H`: open menus, then arrow keys
  - `Ctrl+Shift+K`: edit any key binding; saved to `~/.config/audio-editor/keymap.xml`
  - `Ctrl+,`: audio device settings

## Build (Arch Linux)
```sh
sudo pacman -S --needed base-devel git cmake ninja alsa-lib jack2 freetype2 fontconfig \
  libx11 libxrandr libxinerama libxcursor libxcomposite mesa
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/AudioEditor_artefacts/Release/Audio\ Editor
```
JUCE 8.0.15 is fetched automatically; pass `-DJUCE_DIR=/path/to/JUCE` to use a local checkout.

On Hyprland the current JUCE UI runs through XWayland. For sharp rendering with fractional
scaling, set `xwayland { force_zero_scaling = true }` in `hyprland.conf`.

## License
GNU Affero General Public License v3.0 only. See `LICENSE`. JUCE is used under its AGPLv3 license.
