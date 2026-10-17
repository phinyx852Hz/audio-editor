# PHINYX

A keyboard-first audio editor and mastering suite for Linux, combining the best of
Adobe Audition, Sound Forge and WaveLab. Optimized for Arch Linux, Omarchy and Hyprland.

Status: Phase 1 complete: a usable editor with markers, meters, spectrogram, themes and a plugin Master Section. Not yet tested on real audio hardware. See the full plan in [docs/plan.md](docs/plan.md).

Built with a Qt 6 interface (native Wayland) on top of a JUCE 8 audio engine.

## What works now
- Open WAV/AIFF/FLAC/Ogg/MP3 (`Ctrl+O`, or pass a file on the command line); save as WAV/FLAC/AIFF/Ogg (`Ctrl+S`, `Ctrl+Shift+S`) with bit depth and TPDF dither. WAV markers (cue points and regions) are kept.
- Playback through ALSA or JACK/PipeWire (`Space`), cursor movement (`Left`/`Right`, `Ctrl+Left`/`Ctrl+Right`, `J`/`L`, `Home`/`End`), zoom (`+`/`-`, `Ctrl+0`, `Ctrl+E` to the selection, `Ctrl`+mouse wheel)
- Selection by mouse drag, `Shift`+click, `Shift`+arrows/`J`/`L`/`Home`/`End`, `Ctrl+A`; `Esc` clears it
- Editing: cut/copy/paste/delete (`Ctrl+X`/`C`/`V`, `Del` or `Backspace` on a selection; select by dragging, `Shift`+arrows or `Ctrl+A`), trim to selection (`Ctrl+T`), insert silence (`Ctrl+Alt+S`)
- Processing (on the selection, or the whole file when nothing is selected): gain (`Ctrl+G`), normalize to peak dBFS or integrated LUFS (`Ctrl+Shift+N`), fade in/out with four curve shapes (`Ctrl+Shift+I`/`O`), reverse (`Ctrl+Shift+R`), invert (`Ctrl+Alt+I`), DC offset removal (`Ctrl+Alt+D`)
- Unlimited undo/redo (`Ctrl+Z`, `Ctrl+Shift+Z`) and a History panel (`Ctrl+H`, `Enter` jumps to a step, `Esc` returns to the waveform); you are asked before unsaved changes are lost
- Markers and regions: `M` adds a marker at the cursor, `Shift+M` makes a region from the selection, `'` and `;` jump to the next/previous marker. The Markers panel (`Ctrl+Shift+M`) lists them: `Enter` jumps, `F2` renames, `Del` removes. Markers are saved inside WAV files.
- Spectral display (`Shift+D`): log-frequency spectrogram of every channel, still fully editable
- Meters (`Ctrl+Shift+L`): sample peak bars with peak hold, true peak, and momentary/short-term/integrated loudness (ITU-R BS.1770 / EBU R128) during playback; `Ctrl+Alt+R` resets them. Loudness statistics of the selection or file (`Ctrl+Shift+A`): integrated LUFS, loudness range, true peak.
- Panels (Master Section, Meters, Markers, History) dock in the window or pop out into their own window with `Ctrl+Alt+P`, so Hyprland tiles them like any other window; all shortcuts work there too. The layout is remembered.
- Master Section (`Ctrl+Shift+E`), like WaveLab's: 8 plugin slots plus master gain, applied to everything you play. Hosts VST3, LV2 and LADSPA effects (scan with `Ctrl+Alt+Shift+S`, add with `Ctrl+Alt+E`). In the slot list `Enter` opens a keyboard-friendly parameter editor (type to filter, arrows to adjust), `E` opens the plugin's own window (needs XWayland), `B` bypasses, `Del` removes, `Ctrl+Up`/`Down` reorders. `Ctrl+Shift+B` bypasses the whole section. Render (`Ctrl+Shift+Enter`) applies it to the audio as an undoable edit, or writes a new file with bit depth and dither; plugin latency is compensated. The chain is remembered between sessions.
- Window opacity: slider at the bottom right or View > Window Opacity; `Ctrl+Alt+-` and `Ctrl+Alt+=` change it. Your compositor's own opacity (for example a Hyprland window rule) still applies on top.
- Waveform colours (`Ctrl+Shift+W`, Options > Waveform Colors): pick your own waveform, background, selection, cursor, centre line, marker and region colours; they apply on top of any theme and "Theme default" puts one back
- Themes (`Ctrl+Shift+T`): PHINYX Dark and Light built in, plus your own JSON themes in `~/.local/share/phinyx/themes` (see [docs/themes.md](docs/themes.md))
- Every action is a command with a keybinding:
  - `Ctrl+Shift+P` or `F1`: command palette, searches every command and shows its shortcut
  - `F10` or `Alt+F/E/P/M/A/S/T/V/O/H`: open menus, then arrow keys
  - `Ctrl+Shift+K`: change any key binding, with conflict detection; saved to `~/.config/phinyx/keymap.ini`
  - `Ctrl+,`: choose the audio output device

## Build and run (Arch Linux)
```sh
sudo pacman -S --needed base-devel git cmake ninja qt6-base qt6-wayland alsa-lib ladspa \
  freetype2 fontconfig libx11 libxrandr libxinerama libxcursor libxcomposite libxext mesa
git clone https://github.com/phinyx852Hz/audio-editor
cd audio-editor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/phinyx_artefacts/Release/phinyx
```
Run the core tests with `ctest --test-dir build`.
Do not install `jack2` on Omarchy or any PipeWire system: it conflicts with `pipewire-jack`, which PHINYX uses for JACK output. Keep `pipewire-jack`.

JUCE 8.0.15 is downloaded automatically during the first configure (the freetype/X11 packages above are only needed to build JUCE's small `juceaide` helper); pass `-DJUCE_DIR=/path/to/JUCE` to use a local checkout.

For plugins, `sudo pacman -S lsp-plugins` gives a large free mastering set (EQ, compressors, limiters).

To install system wide instead, use the PKGBUILD: `cd packaging/arch && makepkg -si`.

## Hyprland
PHINYX runs natively on Wayland. Its window class is `phinyx`, so you can target it in `hyprland.conf`, e.g.
`windowrulev2 = workspace 3, class:^(phinyx)$`. Popped-out panels share that class and have titles like
`Meters - PHINYX`, so you can place them separately, e.g. `windowrulev2 = monitor 1, title:^(Meters - PHINYX)$`.

## License
GNU Affero General Public License v3.0 only. See `LICENSE`. JUCE is used under its AGPLv3 license; Qt under the LGPLv3.
