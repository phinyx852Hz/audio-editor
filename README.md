# PHINYX

A keyboard-first audio editor and mastering suite for Linux, combining the best of
Adobe Audition, Sound Forge and WaveLab. Optimized for Arch Linux, Omarchy and Hyprland.

Status: Phase 1 complete: a usable editor with markers, meters, spectrogram, themes and a plugin Master Section. Not yet tested on real audio hardware. See the full plan in [docs/plan.md](docs/plan.md).

Built with a Qt 6 interface (native Wayland) on top of a JUCE 8 audio engine.

## What works now
- Open WAV/AIFF/FLAC/Ogg/MP3 (`Ctrl+O`, or pass a file on the command line); save as WAV/FLAC/AIFF/Ogg (`Ctrl+S`, `Ctrl+Shift+S`) with bit depth and TPDF dither. WAV markers (cue points and regions) are kept.
- Playback through ALSA or JACK/PipeWire (`Space`), cursor movement (`Left`/`Right`, `Ctrl+Left`/`Ctrl+Right`, `J`/`L`, `Home`/`End`), zoom (`+`/`-`, `Ctrl+0`, `Ctrl+E` to the selection, `Ctrl`+mouse wheel)
- Selection by mouse drag, `Shift`+click, `Shift`+arrows/`J`/`L`/`Home`/`End`, `Ctrl+A`; `Esc` clears it
- Tabs: each open file gets its own tab above the waveform, with its own undo history, selection and zoom. `Ctrl+O` opens into a new tab, `Ctrl+Tab`/`Ctrl+Shift+Tab` (or `Ctrl+Page Down`/`Up`) switch tabs, `Ctrl+W` closes one. Copy and paste work between tabs.
- Any channel count, from mono to 5.1, 7.1 and 7.1.4: each lane and meter is labelled with its speaker (L, R, C, LFE, Ls, Rs...), the status bar names the layout, loudness weights the surrounds per ITU-R BS.1770, and the Master Section, render and save keep every channel (surround WAVs carry their speaker layout). Surround files are heard folded down to stereo (centre and surrounds at -3 dB, LFE left out), which suits headphones; Options > Play Surround on Separate Outputs sends each channel to its own output when the device has enough. A plugin that only handles stereo processes the front L/R and its card says "L/R only". Pasting between layouts puts channels in the same positions.
- Record (`Ctrl+R`), like OBS: record what you hear on the desktop (any output, e.g. AirPods or speakers), any input (microphone, audio interface), or just one application (only the browser, only a game). The list updates as devices and apps come and go. Live meters with a clip light (click it to clear), a pre-record buffer that keeps the last few seconds before you pressed Record, and a disk-space estimate. The take goes into a new tab or at the cursor as an undoable edit. `Enter` starts and stops. While recording, a safety copy is written to `~/.cache/phinyx/recordings`; if PHINYX closes unexpectedly, the next start offers to recover it into `~/Music/PHINYX Recovered`. Needs PipeWire (the Arch and Omarchy default).
- Copy to New (`Shift+C`), like Audition and WaveLab: puts the selection (or the whole file) in a new untitled tab, markers included
- Editing: cut/copy/paste/delete (`Ctrl+X`/`C`/`V`, `Del` or `Backspace` on a selection; select by dragging, `Shift`+arrows or `Ctrl+A`), trim to selection (`Ctrl+T`), insert silence (`Ctrl+Alt+S`)
- Processing (on the selection, or the whole file when nothing is selected): gain (`Ctrl+G`), normalize to peak dBFS or integrated LUFS (`Ctrl+Shift+N`), fade in/out with four curve shapes (`Ctrl+Shift+I`/`O`), reverse (`Ctrl+Shift+R`), invert (`Ctrl+Alt+I`), DC offset removal (`Ctrl+Alt+D`)
- Unlimited undo/redo (`Ctrl+Z`, `Ctrl+Shift+Z`) and a History panel (`Ctrl+H`, `Enter` jumps to a step, `Esc` returns to the waveform); you are asked before unsaved changes are lost
- Markers and regions: `M` adds a marker at the cursor, `Shift+M` makes a region from the selection, `'` and `;` jump to the next/previous marker. The Markers panel (`Ctrl+Shift+M`) lists them: `Enter` jumps, `F2` renames, `Del` removes. Markers are saved inside WAV files.
- Spectral display (`Shift+D`): log-frequency spectrogram of every channel, still fully editable
- Meters (`Ctrl+Shift+L`): sample peak bars with peak hold, true peak, and momentary/short-term/integrated loudness (ITU-R BS.1770 / EBU R128) during playback; `Ctrl+Alt+R` resets them. Loudness statistics of the selection or file (`Ctrl+Shift+A`): integrated LUFS, loudness range, true peak.
- Panels (Master Section, Meters, Markers, History) dock in the window or pop out into their own window with `Ctrl+Alt+P`, so Hyprland tiles them like any other window; all shortcuts work there too. The layout is remembered.
- Master Section (`Ctrl+Shift+E`), like WaveLab's: 8 plugin slots plus master gain, applied to everything you play. Hosts VST3, LV2 and LADSPA effects (scan with `Ctrl+Alt+Shift+S`, add with `Ctrl+Alt+E`). In the slot list `Enter` opens a keyboard-friendly parameter editor (type to filter, arrows to adjust), `E` opens the plugin's own window (needs XWayland), `B` bypasses, `Del` removes, `Ctrl+Up`/`Down` reorders. `Ctrl+Shift+B` bypasses the whole section. Render (`Ctrl+Shift+Enter`) applies it to the audio as an undoable edit, or writes a new file with bit depth and dither; plugin latency is compensated. The chain is remembered between sessions.
- Device chain (`Ctrl+Shift+C`), like Ableton and Bitwig: the Master Section's plugins as cards along the bottom of the window, left to right into the master gain. Each card has on/off, solo (hear only that plugin while playing), its first knobs, a level bar, Edit and Window buttons. Keys: `Left`/`Right` pick a card, `Up`/`Down` pick a knob, `+`/`-` turn it (`Shift` or `Page Up`/`Down` for bigger steps), `F2` or just typing a number sets an exact value (so does double-clicking a knob; `Enter` sets it, `Esc` cancels), `Enter` edits all parameters, `Insert` adds a plugin after the card, `Del` removes it, `B` bypasses, `S` solos, `E` opens the plugin's window, `Ctrl+Left`/`Right` moves it, `Esc` goes back to the waveform. Drag a card by its name to move it with the mouse.
- Focus follows the mouse (View > Focus Follows Mouse, on by default), like Hyprland: point at the waveform, the device chain or a panel and your keys go there, no click needed. It never pulls focus out of a text field or dialog.
- Window opacity: slider at the bottom right or View > Window Opacity; `Ctrl+Alt+-` and `Ctrl+Alt+=` change it. Your compositor's own opacity (for example a Hyprland window rule) still applies on top.
- Waveform colours (`Ctrl+Shift+W`, Options > Waveform Colors): pick your own waveform, background, selection, cursor, centre line, marker and region colours; they apply on top of any theme and "Theme default" puts one back
- Themes (`Ctrl+Shift+T`): PHINYX Dark and Light built in, plus your own JSON themes in `~/.local/share/phinyx/themes` (see [docs/themes.md](docs/themes.md))
- Every action is a command with a keybinding:
  - `Ctrl+Shift+P` or `F1`: command palette, searches every command and shows its shortcut
  - `F10` or `Alt+F/E/P/M/A/S/T/V/O/H`: open menus, then arrow keys
  - `Ctrl+Shift+K`: change any key binding, with conflict detection; saved to `~/.config/phinyx/keymap.ini`
  - On Hyprland (Omarchy), PHINYX reads the desktop's own key bindings (`hyprctl binds`) and warns when one of its shortcuts is already taken, since those keys never reach the app. PHINYX never uses the Super key, which Omarchy keeps for itself.
  - `Ctrl+,`: choose the audio output device

## Build and run (Arch Linux)
```sh
sudo pacman -S --needed base-devel git cmake ninja qt6-base qt6-wayland alsa-lib libpipewire ladspa \
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
