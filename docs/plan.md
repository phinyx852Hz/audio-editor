# PHINYX: Plan

Name: **PHINYX** (system id `phinyx`; repository: github.com/phinyx852Hz/audio-editor)
Target: native desktop app, Linux only, optimized for Arch Linux, Omarchy and Hyprland (x86_64 first, aarch64 later)
Goal: combine the basic and best features of Adobe Audition, Sound Forge and WaveLab in one app.
License: AGPLv3 open source

## Requirements at a glance
| Requirement | Section |
|---|---|
| Every menu and process has a key binding and works fully by keyboard | 2.8 |
| Audition-style spectral editing with Auto Heal and Spot Healing Brush | 2.2 |
| Every Linux plugin format (LV2, VST3, CLAP, LADSPA, DSSI, VST2, Windows plugins via yabridge) | 2.7 |
| WaveLab-style Master Section that pops out to its own tile | 2.4 |
| A/B reference section like WaveLab's Montage, with at least two tracks and no mixer | 2.3, 2.4 |
| WaveLab-style Analyze menu: File Comparator, 3D Frequency, Global, Loudness, Visual Analysis | 2.4b |
| Advanced transcoding like WaveLab, export to every format that has a Linux encoder | 2.5 |
| Batch processor like WaveLab | 2.6 |
| Audition-style metadata editor and writer with multiple schemas | 2.6b |
| Markers | 2.3b |
| Multiple GUI themes, skinnable, third-party themes like REAPER | 2.8b |
| MIDI mapping to any hardware, `Ctrl+M` mapping mode like Ableton | 2.9 |
| Scarlett / Clarett integration based on Geoffrey Bennett's Linux driver and GUI | 2.9b |
| Ableton-style library and browser that searches files, plugins, stock effects and presets | 2.9c |
| Optimized for Arch, Omarchy and Hyprland, with panels that pop out to tiles | 3 |
| Multi-channel (5.1 to 7.1.4) and Ambisonics, stereo upmix, built-in mono-to-stereo | 2.11 |

## Where things stand (2026-10-17)
- **Plan:** this document, covering every requirement above.
- **Code:** github.com/phinyx852Hz/audio-editor. Phase 0 foundation, rebuilt on Qt 6 (native Wayland) with a JUCE audio engine underneath. It opens a file, shows the waveform, plays it through ALSA or JACK/PipeWire, and every action is a remappable command: command palette (`Ctrl+Shift+P` / `F1`), menus by `F10` / `Alt`+letter, shortcut editor with conflict detection (`Ctrl+Shift+K`). Tested by keyboard on a virtual X11 display; not yet run on a real Wayland/Hyprland session or with a real sound device.
- **Also in place:** Arch + Ubuntu CI, an AUR-style PKGBUILD (`phinyx-git`), README.
- **Decided:** AGPLv3; Qt 6 UI + JUCE engine; name PHINYX.
- **Phase 1 Part A (done):** real editing. Selection, cut/copy/paste/delete/trim, insert silence, gain, normalize (peak or LUFS), fades with four shapes, reverse, invert, DC removal, unlimited undo with a History panel, Save/Save As to WAV/FLAC/AIFF/Ogg with bit depth and dither, WAV markers preserved, unsaved-changes prompts. Core tests (storage, undo, DSP, EBU loudness reference) run in CI.
- **Phase 1 Part B (done):** markers and regions (keys, Markers panel, saved in WAV), live peak/true-peak/LUFS meters, loudness statistics, log-frequency spectral display, panels that pop out into their own Hyprland-tiled windows with the layout remembered, colour themes with third-party JSON themes (docs/themes.md).
- **Phase 1 Part C (done):** Master Section with 8 plugin slots (VST3, LV2, LADSPA via JUCE hosting), master gain and bypass, keyboard parameter editor plus the plugin's own window under XWayland, render in place (undoable) or to a new file with dither, latency compensated, chain remembered. Verified with LSP plugins.
- **Phase 1 status:** core done. Still open from the Phase 1 list: Master Section presets, split waveform/spectral view and FFT settings. Added after the owner's first Omarchy test: Delete shows a hint when nothing is selected, window opacity control, waveform colour picker. Device chain row (2.4) done, with slot solo. Not yet tried on real audio hardware or a real Hyprland session (next: the owner's Omarchy test).
- **Phase 1.5 Part A (done):** recording and capture like OBS (2.1b P1): `Ctrl+R` records desktop audio, any input or one application through native PipeWire, with live meters and clip light, pre-record buffer, disk-space estimate, a new tab or insert at the cursor, and a crash-safe copy recovered on the next start. Tested against a real PipeWire server (desktop monitor, an app stream, the app ending mid-take, a killed process). Still open from 2.1b P1: several sources at once, bit depth choice (takes are 32-bit float until saved), plain-ALSA input without PipeWire.
- **Phase 1.5 Part B (done):** any channel count (up to 16 played and metered). Speaker layouts by channel count in WAV order (mono to 7.1.4), labelled lanes and meters, BS.1770 surround weights, stereo fold-down monitoring by default with an opt-in to separate outputs (reordered for ALSA devices), Master Section and render at the file's channel count (stereo-only plugins process L/R, the rest pass through), WAV channel mask on save, sensible channel mapping on paste, record in quad, 5.1 or 7.1. Tested: mono, 5.1 and 7.1 tones through a real PipeWire server, fold-down and separate outputs measured per speaker, 5.1 render to a new file. Still open from 2.11: changing a file's layout (upmix, downmix to a new file, reorder), per-channel solo/mute while listening, layouts read from the WAV channel mask instead of assumed.
- **Phase 1.5 Part C (done):** built-in Stereoize (`Ctrl+Alt+W`): complementary delay on the high-passed mid signal, so left + right equals the original exactly (mono-compatible), with width, delay and keep-bass-centred settings remembered; on a mono file the whole file becomes stereo, the effect applies to the selection; undoable. Phase 1.5 is complete.
- **Stereoize as a live effect (done, owner request 2026-10-17):** Stereoize also loads into the Device Chain / Master Section as a built-in effect (format "Built-in") with Width, Delay and Bass knobs that work while playing, type-in values, save/restore with the chain, and a click-free delay change. A mono file is now heard on both sides, so widening is audible, and Master Section renders of a mono file come out stereo when the chain makes the sides differ (otherwise they stay mono). Apply to Audio now splices a selection's render back into the file instead of replacing the file with it.
- **Phase 2 plan (owner said go, 2026-10-17):** Part A spectral selection, spectral edits and Auto Heal; Part B noise reduction from a learned profile plus DeClick, DeClip, DeHum, DeEss; Part C Spot Healing Brush, lasso and brush selection, harmonics, find-similar; Part D Diagnostics scan panel; Part E CLAP/LADSPA/yabridge and out-of-process scanning, sample-rate and bit-depth conversion, metadata editor.
- **Phase 2 Part A (done):** Repair menu. Box selection of time and frequency in the spectral view (log scale, same mapping as the display; a near full-height drag means every frequency), typed band (`Ctrl+Alt+F`), All Frequencies (`Ctrl+Alt+Shift+F`). Engine in `Source/core/Repair.cpp`: STFT (periodic Hann, 75 % overlap) where only the change is overlap-added back, so audio outside the box and its feather (1/6 octave, window length in time) is bit-identical. Auto Heal (`Ctrl+U`, up to 4 s): per bin, robust level and phase turn learned from 6 frames each side; steady bins continue their phase from both sides (blended), noisy bins are refilled with fresh noise at the learned level; window 256 to 2048 samples sized to the gap (4096 for a band). Spectral Gain (`Ctrl+Alt+G`) and Delete in a box. Tests: click, 0.3 s cough over a tone (error at the hiss level, hiss level kept within 1 dB), 5 kHz ring healed by band with the 300 Hz tone untouched, band removal -80 dB. Still open from 2.2: copy/paste of spectral regions, apply an effect or plugin to a box, pitch display, FFT settings, mel/linear scales.
- **Phase 2 Part B (done):** `Source/core/Restoration.cpp`. Noise Reduction: learned per-channel noise spectrum (2048-point STFT), soft gate (no gain up to the sensitivity above the noise, full gain 6 dB beyond it), 3-bin smoothing, instant attack and set release, starting at full reduction; settings reduction/sensitivity/release. DeClick: AR(20) prediction error per 64k block, flags above a robust local spread (Low/Medium/High), each click (up to 2.5 ms) rebuilt by least-squares AR interpolation (banded Cholesky). DeClip: runs of 2+ samples at the clip level rebuilt jointly by the same interpolation with two model passes, never below the clip level; reports the new peak so the user can Normalize. DeHum: finds 50/60 Hz to 0.01 Hz (Goertzel over three harmonics), narrow notch cascade per harmonic, filters settled on the audio before the range or, at the file start, on the measured hum. DeEss: 1024-point STFT, band from a set frequency, reduction above a threshold, at most a set amount, 100 dB/s release. Long ranges are processed in pieces with a progress bar and Cancel. Tests: hiss under a tone -17 dB with the tone kept, 40 of 40 clicks with almost no false hits on clean audio, a clipped sine rebuilt, hum gone from the first sample, an esses burst turned down 10 dB. Still open from 2.2: live preview in the dialogs, adaptive noise learning, spectral-box versions of these tools.
- **Phase 2 Part C (done):** drawn selections as a `SpectralMask` (columns of 256 frames by 1/24-octave rows from 20 Hz, values 0 to 1): brush dabs are ellipses with a soft outer quarter, the lasso fills its polygon (even-odd rule), Add Harmonics copies each row up by log2(h) octaves for 2 to 8 times the frequency. Spectral Gain uses the mask's window-weighted average per frame and bin; Auto Heal its peak over the window, so nothing selected leaks through. Tools in the Repair menu: Box `V`, Lasso `O`, Brush `B`, Spot Healing Brush `H` (heals on release), `[`/`]` size, `Shift` adds; the shape is drawn as a tint and stays per tab. Find Similar (`Ctrl+Shift+F`, strictness 50 to 99 %): 1024-point frames every 256 samples, sixth-octave band levels over the example's band, excess over a rolling median (±1 s) background, the example's pattern slid along with cosine similarity at least the strictness and at least half its strength, best per neighbourhood; then Heal All (one undo step) or Mark Them as regions. Tests: brush, lasso and harmonics coverage, lasso removal of a 3 kHz tone with the rest bit-identical, spot heal of a 5 kHz ring keeping the 300 Hz tone, find-similar finds 5 of 5 chirps past a low thump and nothing in plain audio. Verified in the app: spot heal of one chirp, find-similar healing all six, lasso plus harmonics deleting a whistle with two overtones.
- **Phase 2 Part D (done):** Diagnostics panel (`Ctrl+Shift+D`, Repair and View menus, dockable or popped out). Scans that change nothing (`findClicks`, `findClipping`, `findDropouts`, `findSilence` in `Restoration.cpp`) share their detection with DeClick and DeClip; events on several channels are joined into one row listing the channels. Dropouts: digital silence from 1 ms to 1 s with sound (above -60 dBFS) on both sides. Silence: below a level (RMS over 10 ms) for at least a set time. Moving through the list selects and zooms to each problem; Repair, Repair All (`repairProblems`: nearby problems are fixed together in one stretch with 4096 samples of context and the rest of the audio is shared, clicks and short dropouts by AR interpolation, clipping by the joint rebuild, dropouts over 512 samples by Auto Heal), Mark (regions) and Delete for silence (markers follow). The list is dropped when the audio changes some other way. Tests: 40 clicks listed and repaired from the list, clipping listed and rebuilt, three dropouts found exactly (silence at the file start is not one) and filled, a silent stretch found within 10 ms. Verified in the app: 25 of 25 clicks found and repaired on dirty.wav, a quiet second found and deleted.
- **Phase 2 Part E1 (done):** Convert Sample Type (`Ctrl+Alt+T`, Process menu): `dsp::resample` is a Kaiser-windowed sinc (beta 9.6, 32 zero crossings, cutoff at 95 % of the lower Nyquist, tabulated at 1/4096 with linear interpolation), `dsp::toMono` averages the channels, `dsp::quantize` rounds to 8 to 24 bits with none, TPDF, or TPDF with second-order noise shaping (error fed back through (1 - z^-1)^2, bounded). One undo step; markers scale with the rate; Save then defaults to the chosen depth. Tests: 1 kHz from 48 to 44.1 kHz within 2e-4 of the ideal, no alias from a 10 kHz tone resampled to 16 kHz, 48 to 96 to 48 kHz returns the original, dithered 16-bit on the grid within the dither, shaping cuts the low-frequency error more than tenfold. Chosen over libsamplerate/soxr to avoid a new dependency; quality is in the same class.
- **Phase 2 Part E2 (done):** metadata. `Tags` (schema-neutral keys in `AudioDocument.h`) are part of each undo snapshot, so edits are undoable and mark the file changed. Loading and saving map them per format: WAV through JUCE's LIST/INFO (INAM, IART, IPRD, GENR, ICRD, TRCK, ICMT, ICOP, plus ISFT=PHINYX) and bext; Ogg through JUCE's Vorbis comments (title, artist, album, comment, date, genre, track); FLAC through our own Vorbis comment block reader/writer (`Source/core/FlacTags.cpp`, since JUCE's FLAC codec has none), which rewrites the metadata blocks and keeps stream info and audio. Metadata dialog (`Ctrl+Alt+M`): Tags and Broadcast Wave tabs, Fill In Now, a note on what the file type keeps. Verified with ffprobe on saved WAV and FLAC. Still open from 2.6b: ID3 and MP4 (needs TagLib), iXML beyond JUCE's ASWG fields, cover art, BWF loudness fields, templates, batch.
- **Phase 2 Part E3 (done):** plugin hosting. Out-of-process scanner: `MasterChain::scanNext` runs `phinyx --scan-plugin <format> <file> <out.xml>` with `juce::ChildProcess` (60 s timeout, 180 s for yabridge paths since Wine has to start); a crash, hang or non-zero exit blacklists the file, up-to-date listings are skipped, and LV2 bundles (which JUCE lists once per plugin) are checked once. CLAP hosting is our own `ClapPluginFormat` (`Source/engine/ClapFormat.cpp`, CLAP SDK headers only, from the system, `-DCLAP_DIR` or fetched at 1.2.7): factory scan with per-plugin descriptions, audio ports (main pair carries the audio, other ports get silence), parameters as normalised `HostedAudioProcessorParameter`s with values queued to the next `process()` (or `flush()` while inactive) and plugin-side changes read back from output events, state save/load, latency, `request_restart`/`request_callback`, thread-check, log, timer-support and posix-fd-support, and the plugin's X11 window embedded with `XEmbedComponent`. Activation is on the message thread (renders call it through `MessageManager::callSync`) with an 8192-frame maximum so listening and rendering share one activation. yabridge needs no code: its VST3 and CLAP copies are found by the recursive search of `~/.vst3` and `~/.clap`; VST2 is not hosted. Verified: 561 plugins scanned in 22 s (19 CLAP files, 195 CLAP effects incl. LSP's 176-plugin bundle); MVerb and LSP Limiter Stereo (CLAP) load, show their own windows, knobs move both ways between the plugin window and the chain card, render applies the reverb at 48 kHz while the device runs at 44.1 kHz, and settings come back after a restart. Not done: CLAP note ports/MIDI, audio-ports-config (surround CLAP plugins get stereo), preset discovery.
- **Phase 3 step 1 (done): stock plugin frame and bundling.** `cmake/StockPlugins.cmake` builds ToneShiftEQ, Wolf Shaper, LSP Clipper and LSP Multiband Clipper from pinned commits as CLAPs (ExternalProject, make in each repository; option `PHINYX_STOCK_PLUGINS`, on by default) into `build/stock`, installed to `lib/phinyx/stock` with each licence. `Source/engine/StockPlugins.cpp` lists them (credit, licence, footer parameters, display names) and finds the folder (`PHINYX_STOCK_PATH`, then next to the installed binary, then the build folder). `MasterChain` scans them out of process once and caches them in `~/.config/phinyx/stock-plugins.xml` (rescanned when a file changes), lists them as Built-in, and reloads saved slots from wherever the stock folder now is. `StockFrame` (JUCE, coloured from the PHINYX theme via `AudioEngine::setPluginFrameColours`) wraps the plugin's own editor: header with name, credit and licence, A/B compare (state swap kept per slot, Copy), On/Off; input and output stereo peak meters (-60 to 0 dB, 20 dB/s fall, 1 s hold) fed from `processChain`; footer potentiometers for the main controls, which are also the card's knobs. Verified in the app: all four in the list, ToneShiftEQ, Wolf Shaper (OpenGL) and LSP Clipper open in the frame, footer and card knobs move together, A/B swaps settings, meters move while playing, settings come back after a restart and from a moved stock folder. Not done yet: presets and undo/redo in the header, oversampling switch, a theme-driven JUCE LookAndFeel for the JUCE plugin families (comes with ZL and Dusk).
- **Stock oscilloscope (done, requested 2026-10-18):** LSP Oscilloscope x1, x2 and x4 (github.com/lsp-plugins/lsp-plugins-oscilloscope, LGPL-3.0-or-later, GPL-3.0 compatible), bundled like the clippers, with footer knobs Time Division, Vertical Division, Trigger Level and Freeze (footer names may list alternatives, "A|B"). Verified in the app: a 1 kHz tone traced in the frame. Its second channel and the X/Y and external-trigger inputs are extra CLAP ports that PHINYX feeds silence for now, so the x2 shows the left channel on Ch1. Considered: x42's sisco (GPL-2.0, LV2 only).
- **Fixed (2026-10-18): crash when closing a plugin window.** Reported by the owner with a core dump: `_XError` jumped to an unloaded library. LSP plugins install a process-wide X error handler; when the module was released its library unloaded with the handler still installed, and the next X error (XEmbed teardown's XSync) crashed. Now CLAP libraries stay loaded until PHINYX quits (never unloaded, not even at exit), and PHINYX puts its own handler back (`x11::useHostErrorHandler`) after the plugin creates, shows or destroys its window. The teardown order (`gui->destroy` before `removeClient`) stays, as the CLAP spec asks. Verified with gdb: after loading, opening and removing LSP Clipper, the handler pointed at unmapped memory before the fix and at PHINYX's handler after it.
- **Autosave and crash recovery (done, 2026-10-18):** `Recovery` (`Source/ui/Recovery.cpp`): every 60 s (`recovery/autosaveSeconds`, 0 = off) each modified document is compared with what was last written (audio pointer, markers, tags) and, if changed, written in the background as a 32-bit float WAV with markers and tags plus an .ini (original path, tab name, time) to `~/.cache/phinyx/recovery/<pid>-<document id>`; written to a temporary name and renamed, one write at a time. Files of documents no longer modified (saved, closed) are removed; a clean quit removes all of this run's files. On start, files whose process is gone (or a zombie) are offered: Recover opens each with its original path, marked unsaved, then deletes the copy; Discard deletes; Not Now keeps them. `AudioDocument::id()` names each document and moves with it between tabs. Verified: an edit autosaved within the interval, a kill -9 then restart offered the file, the recovered audio is the edited (inverted) one, and a normal quit left the folder empty. Not kept: undo history, selection and view.
- **Phase 3 step 2 (done, 2026-10-18): mastering basics.** `dsp::TruePeakLimiter`: look-ahead brickwall limiter, all channels linked; per sample the gain each 4x-interpolated peak needs (and the sample peak), a sliding minimum over the 5 ms look-ahead plus 4 samples, instant attack and smooth release, then a box average over the look-ahead so the gain ramps finish as the peak leaves the delay (latency = look-ahead + 6 samples), with a sample-peak clamp last. `dsp::limitTruePeak` runs it offline with the latency removed. `dsp::Quantizer` is the block version of `quantize` (TPDF or shaped dither). Built-in effects (Add plugin, Built-in): **True Peak Limiter** (Input 0 to +18 dB, Ceiling -12 to 0 dBTP, Release 1 to 1000 ms; reports its latency so renders line up) and **Dither** (16/20/24 bits; None, TPDF, Shaped; goes last). Normalize gained loudness presets (Spotify/YouTube/Tidal/Amazon -14, Apple Music -16, Podcast -16, EBU R128 -23, ATSC A/85 -24, Custom), Match a File (measures another file's integrated loudness and uses it as the target), and "Limit the true peak to" (default -1.0 dBTP): when turning up would pass the ceiling it limits, re-measures and turns up by what is missing, up to four passes, and says so if the target cannot be reached without more limiting; choices are remembered. Tests: a loud tone and an fs/4 tone with peaks between samples come out under -1 dBTP; quiet audio passes unchanged and in time; a partial range is limited only there. Verified in the app: -14 LUFS preset hit exactly; -6 LUFS stopped at -7.2 LUFS with -1.0 dBTP and the message; limiter + dither in the chain rendered at -1.0 dBTP. EQ decision: ToneShiftEQ (stock, minimum-phase FFT and biquad engines) is the built-in EQ for now; a linear-phase EQ stays on the list below.
- **Audacity features (done, requested 2026-10-18):** "incorporate select items from Audacity, in the PHINYX theme, with hover showing the shortcut: the transport controls centre top, time display, the Effect menu and the Tools menu with the Nyquist stuff." (1) `TransportBar` (Source/ui/TransportBar.cpp): round theme-coloured buttons drawn in code (Go to Start, Play/Pause, Stop, Go to End, Record, Loop), each wired to its command, tooltips built on hover from the live keymap ("Stop (K)"); `TimeDisplay` with four formats (h m s.ms, seconds, samples, CD frames), remembered. Loop playback (`transport.loop`, Ctrl+L) wraps inside `DocumentSource` over the selection or the whole file; Stop now returns to where playback started, Space pauses in place. The Device Chain is also saved with each autosave. (2) Nyquist: libnyquist vendored from Audacity 3.7.9 (`third_party/libnyquist`, BSD-style; PortAudio stubbed by `third_party/nyquist-shim`; built by `cmake/Nyquist.cmake`), the Nyquist runtime in `share/nyquist`, and Audacity's GPL-2.0-or-later plug-ins in `share/nyquist-plug-ins` (the GPL-2.0-only ones are left out; an installed Audacity's are picked up at run time). `Source/core/Nyquist.cpp` ports Audacity's NyquistBase: header tokenizer and controls (real, int, float-text, int-text, time, string, choice incl. id/label pairs, file, text), version 1 to 4 variables (*TRACK*, *SELECTION* incl. the spectral band, *PROJECT*, *SYSTEM-DIR*, *SYSTEM-TIME*), Lisp and SAL, mono/stereo passes (more channels one at a time), labels, messages, numbers; one script at a time on a worker thread with a C numeric locale, cancel via the OS callback. `_`/`NGETTEXT` registered; `AUD-DO` answers that it is not available. UI (`Source/ui/NyquistUi.cpp`): Effect menu (with a Generate submenu and Repeat Last Effect, Ctrl+Shift+Y), Nyquist analyzers at the end of Analyze, Tools menu (Nyquist Prompt Ctrl+Alt+Y with Load/Save/Debug and Ctrl+Enter, tool plug-ins, open the plug-ins folder, rescan); each plug-in is a command `nyquist.<file>`; control dialogs remember values; labels become markers; generators with nothing open make a new file. Tests: header parsing, process, labels, message, peak, generator, error text, Audacity's Tremolo. Verified in the app: Effect, Tools and Analyze menus, Tremolo dialog and result, Nyquist Prompt fade, Regular Interval Labels to markers. Not yet: Preview button, AUD-DO, Audacity's built-in (C++) effects such as Echo or Paulstretch in the Effect menu, applying a single plugin from the Effect menu.
- **Editor look (done, requested 2026-10-18):** "remove audacity look of transport and time bar, and remove background on both... standard font... 00:00:00.000"; "remove the bottom display of the time and other metadata... put it at the bottom of the main editor display... it actually moves back and forth sideways when the numbers change"; the circular three-arrow symbol in white for Loop; "a time display grid... that changes to minutes when zooming out and microseconds when zooming in"; "a top macro view" (WaveLab's overview), with "zoom in and out by placing the mouse cursor anywhere in whole-file overview" (Audition, WaveLab). Done: flat transport symbols in the theme's text colour (accent on hover and when Loop is on), no backgrounds, time `00:00:00.000` in the interface font, format menu removed (alternate views later). `InfoStrip` (Source/ui/InfoStrip.cpp) under the waveform holds the details; each field keeps the width of the longest text it has shown and `steady::draw` gives every digit the widest digit's width, so nothing jiggles; status messages stay in the window's status bar. `TimeRuler` above the waveform picks the smallest step (1 µs to 10 h) whose labels fit, labels "0", "20 s", "1 min 20 s", "4.25 s", "5.71505 s", marks the cursor and selection, click moves the cursor. `Overview` (View > Whole-File Overview, Ctrl+Alt+O, remembered): its own ruler, the whole waveform drawn once per file version, the editor's range boxed with the rest dimmed; drag across it (outside the box, or anywhere when the box covers the whole file) zooms the editor to that stretch, a click centres there, dragging the box moves it, edge drag zooms, wheel zooms around the mouse, double-click moves the cursor.
- **Resident Audio T2 and T4 (requested 2026-10-18):** "add the same integration with the Resident Audio T2 and T4 linux drivers that Claude figured out. Those will need to be finished and posted to GitHub as a separate project." So: (1) the drivers become their own repository, separate from PHINYX, and are finished there; (2) PHINYX gets an Interface panel for them like the Focusrite one in 2.9b (auto-detect, recording controls, routing presets, hardware meters, every control a command, a driver check that links to the driver project's setup steps). Open item: the earlier driver work is not in this project's files or threads, so its code or notes need to be brought in before work starts, and the new repository needs creating on the owner's GitHub account.
- **Phase 3 step 3 (done, 2026-10-18): ZL Audio stock plugins.** Picked over album assembly (offered as a choice; built the recommended option). `cmake/StockPlugins.cmake` builds ZL Equalizer 2, ZL Spectrum Equalizer, ZL Compressor, ZL Limiter and ZL Splitter from pinned commits with their own JUCE fork as VST3s (ExternalProject, Ninja, Clang 17+ found by `find_program`, else left out with a warning; Highway SIMD level `PHINYX_ZL_SIMD`, SSE4 by default, AVX2 or NEON possible), each LICENSE.md and licenses folder copied. Stock plugins may now be VST3s (`stock::Info::format`), scanned and relocated the same way as the CLAPs. Footer knobs: EQs Gain Scale and Output Gain; Compressor Threshold, Ratio, Attack, Release, Makeup Gain, Wet; Limiter Input Gain, Output Ceiling, Release, Lookahead; Splitter Split Type, Mix. `stock::unit` drops a unit label that only repeats the parameter's name (ZL does that); VST3 categories show without "Fx". Build cost: about 133 CPU-minutes for all five (8 minutes wall for one on 8 cores); 55 MB stripped. Verified in the app: all five scanned and listed under Built-in, loaded together in the chain with their footer knobs, ZL Equalizer 2 and ZL Compressor open in the PHINYX frame, playback with all five runs. CI installs clang and libxi; PKGBUILD adds clang (make) and libxi. Found while checking: plugin delay is compensated when rendering but not yet during playback (the cursor runs ahead of the sound by the chain's latency); fix next.
- **Next:** playback latency compensation for the Device Chain, then album assembly (montage with per-track loudness).

---

## 1. What each reference app is best at

| App | Signature strengths worth taking |
|---|---|
| **Adobe Audition** | Spectral frequency display with brush/lasso healing, Diagnostics panel (DeClick, DeClip, DeHum), learned noise print reduction, clip-based multitrack with automation lanes, Essential Sound style task presets, Match Loudness |
| **Sound Forge** | Fast, precise destructive waveform editing, event/region tools, CD/DDP-style disc authoring, sample-accurate editing at deep zoom, low-friction "open, fix, save" workflow, script/batch processing |
| **WaveLab** | Mastering-grade metering (loudness, true peak, phase, spectrum), Master Section signal chain, Montage (non-destructive clip-based assembly for albums), Audio Inpainting, batch processor, render-in-place with dithering/SRC, DDP export, error detection and correction |

The common thread: a **precise single-file editor** (Sound Forge), a **repair lab** (Audition), and a **mastering/assembly desk** (WaveLab).

---

## 2. Consolidated feature list

Priority key: **P0** = MVP, **P1** = v1.0, **P2** = later.

### 2.1 Core waveform editing (Sound Forge core)
- P0 Open/save WAV, AIFF, FLAC, MP3, Ogg/Opus; 16/24/32-bit int, 32/64-bit float; up to 384 kHz; mono to 8+ channels
- P0 Sample-accurate selection, zoom from overview to sample level, snap to zero crossings
- P0 Cut/copy/paste/delete/trim/crop, silence insert, mix-paste
- P0 Editor tabs, one file each with its own history, selection and zoom (done 2026-10-17)
- P0 Copy to New (`Shift+C`, done 2026-10-17): selection or whole file into a new untitled tab, markers included
- P0 Fade in/out with curve shapes, crossfades at edit points
- P0 Normalize (peak and LUFS), gain, invert, reverse, DC offset removal
- P0 Unlimited undo/redo with a history list
- P0 Markers and regions (full spec in 2.3b)
- P1 Pencil tool for redrawing samples
- P1 Channel ops: swap, extract, mix down, mid/side convert
- P1 Sample rate conversion (high-quality SoX/r8brain-style) and bit-depth conversion with dither (TPDF, noise-shaped)
- P1 Loop point and sampler (smpl/inst chunk) editing
- P2 Pitch/time stretching (Rubber Band library)

### 2.1b Recording and capture, like OBS (requested 2026-10-17)
- P1 Record dialog (`Ctrl+R`): pick the source, see live meters, set sample rate, bit depth and channels, record into a new file or at the cursor
- P1 Sources, each picked from a list that updates when devices come and go:
  - **Desktop audio**: everything you hear (the monitor of the current output, e.g. AirPods Max or speakers)
  - **Any input device**: microphone, audio interface (RA T4, Scarlett...), built-in input
  - **One application**: e.g. only the browser or only a game, like OBS's application audio capture
  - Several at once, mixed or kept on separate channels
- P1 Built on PipeWire (the Omarchy/Arch default): native PipeWire capture of sink monitors, nodes and apps, with the JACK route (pipewire-jack) as a fallback; plain ALSA input where PipeWire is missing
- P1 Safety: pre-record buffer (catch the last few seconds before you pressed record), clip warning, disk-space check, crash-safe writing so a crash or power cut keeps what was recorded
- P2 Timer and level-triggered start/stop (start on sound, stop after silence), split into files at silence (handy for vinyl and tape), punch-in (below)

### 2.2 Spectral editing and restoration (Audition core, requested as a headline feature)
Modeled on Audition's Spectral Frequency Display, working on the same audio as the waveform, with every edit undoable.
- P0 Spectral Frequency Display: waveform, spectral, or split view; linear/log/mel frequency scale; adjustable FFT size, window and overlap; color and brightness/contrast controls; zoom on both time and frequency axes
- P0 Spectral Pitch Display (fundamental pitch contour) as an overlay
- P1 Selection tools, all keyboard reachable: time selection, marquee (time + frequency box), lasso, brush (adjustable size and opacity), frequency-band select, and a harmonics option that extends a selection to its overtones
- P1 Edits on a spectral selection: attenuate/boost by dB, delete, copy/paste spectral regions, apply any effect or plugin to only that region, feathered edges to avoid artifacts
- P1 **Auto Heal**: one command (Audition's Ctrl+U equivalent) that repairs a short time or spectral selection by resynthesizing it from surrounding audio, for clicks, coughs, chair squeaks, phone rings
- P1 **Spot Healing Brush**: paint over a sound in the spectrogram and it is healed on release, using the same engine as Auto Heal
- P1 Heal engine design: STFT-domain interpolation from neighboring frames (magnitude and phase), with an inpainting mode for longer gaps; ML inpainting can replace it later without changing the commands
- P1 Find-similar: select one click or noise burst and select all similar events in the file for batch healing
- P1 Noise reduction from a learned noise profile
- P1 DeClick, DeClip, DeHum (50/60 Hz plus harmonics), DeEss
- P1 Diagnostics panel that scans a file and lists problems (clicks, clips, dropouts) for one-click fix
- P2 ML-based dialogue isolation / denoise (ONNX Runtime, optional)

### 2.3 Montage (lightweight, no mixer)
Decided: **no mixer** (no buses, sends or mixing console). The app stays an editor and mastering tool. What it keeps from WaveLab's Montage:
- P1 **At least two tracks for A/B referencing**: track 1 is your audio, track 2 (and optionally more) holds reference material, aligned on the same timeline so A/B switching keeps position (see the A/B Reference section in 2.4)
- P1 Album assembly on a single timeline: clip order, gaps, crossfades, per-clip gain and fades, CD-Text/ISRC, per-track markers
- P1 Per-clip plugin chain feeding the Master Section; no per-track volume/pan automation or routing
- P2 Recording with punch-in (for transfers such as tape or vinyl)

### 2.3b Markers (requirement)
- P0 Marker types: generic, region start/end, loop start/end, CD track start/end/index, mute region, plus user-defined types with color and icon
- P0 Add a marker at the cursor or on the fly during playback (`M`), add a region from the current selection (`Shift+M`), name/rename inline
- P0 Jump to next/previous marker (`'` / `;` by default, remappable), select between markers, snap edits and selections to markers
- P0 Marker list panel: sortable, filterable, editable times and names, all keyboard driven
- P1 Markers survive edits (they move with the audio when you cut or insert)
- P1 Auto markers from silence detection, transient/onset detection, loudness thresholds or a fixed grid
- P1 Render each region or the audio between markers as separate files (feeds batch and transcoding)
- P1 Read and write markers in every container that supports them: WAV `cue`/`labl`/`ltxt`, BWF, Audition XMP markers, AIFF MARK, FLAC/Ogg CUESHEET, ID3 chapters, MP4 chapters
- P1 Import/export CUE sheets, CSV, Audacity label files, REAPER marker CSV
- P2 Markers as MIDI-triggerable targets (jump/loop via the MIDI mapping in 2.9)

### 2.4 Mastering and metering (WaveLab core)
- P0 Meters: peak, RMS, true peak (dBTP), LUFS momentary/short-term/integrated, loudness range (EBU R128 / ITU-R BS.1770)
- P0 **Master Section like WaveLab's** (reference screenshot supplied), a panel that is always in the signal path for playback and render:
  - Header: global bypass (power), monitoring/input toggle, preset name with save/load, render button, settings
  - **Effects pane**: an ordered list of plugin slots (at least 10, growing as needed), each showing a drag handle to reorder, an activity/level indicator, the plugin name, **Solo (S)** to hear only that slot's effect in context, and **bypass**; click a name to open the plugin's own window, `+` to add from the plugin browser
  - Level pane: master gain faders (linked or per channel), mono/stereo/mid-side monitoring, clip indicators
  - Final effects / dithering pane: dedicated last slots for limiter, dither and noise shaping, always after the effects chain
  - Playback processing pane: monitor-only slots (e.g. room correction) that are never rendered
  - Presets for the whole section, A/B/C/D snapshots, chain import/export shared with batch
  - Fully keyboard driven: move between slots with arrows, `Enter` opens the plugin, keys for bypass, solo, move up/down, insert, remove
  - **Pops out to its own window** with one command or button, so on Hyprland it lands in its own tile (see 2.10)
- P1 **Device chain row like Ableton and Bitwig** (requested 2026-10-17; first version done the same day: cards with on/off, solo, first four knobs, level bar, Edit and Window, full keyboard control, drag to reorder; typing exact values by double-click, F2 or digits; still to do: pinning chosen knobs, collapse, gain-reduction display, dropping from the browser): a strip along the bottom of the main window showing the chain left to right as device cards, signal flowing left to right into the master gain and meters
  - Each card: plugin name, on/off, solo, a few main knobs (the plugin's first or user-pinned parameters), a small level/gain-reduction indicator, collapse to a narrow bar, and a button for the full parameter editor or plugin window
  - Same chain as the Master Section list (one model, two views); later reused for per-clip chains in the Montage and batch chains
  - Keyboard: a key focuses the row, `Left`/`Right` move between cards, `Up`/`Down` between knobs, `+`/`-` adjust, `Insert` adds after the focused card, `Ctrl+Left`/`Right` reorders, `Del` removes; drag to reorder with the mouse
  - Drop a plugin from the browser (2.9c) between cards to insert it there; dockable and pop-out like every panel
- P1 Spectrum analyzer, phase correlation meter, goniometer/vectorscope
- P1 Built-in mastering processors, kept to what delivery and renders depend on: parametric EQ (linear and minimum phase), true-peak limiter, loudness targeting, dither and noise shaping. Done (Phase 3 step 2): true-peak limiter, dither and noise shaping, loudness targets; the EQ is ToneShiftEQ for now. Later: a linear-phase EQ. Repair tools (2.2, including the de-esser) are built in too.
- **Decision (owner, 2026-10-17):** everything else (saturators, creative or multiband compressors, stereo width, reverbs, other colour effects) comes from third-party plugins such as LSP rather than being built in. Reason: built-in effects add little size but a lot of work; PHINYX stays focused on editing, repair, metering and delivery.
- P1 Loudness targeting presets (Spotify -14 LUFS, Apple -16, broadcast -23 / -24) with Match Loudness
- P1 **A/B Reference section** (modeled on WaveLab's Montage Reference Tracks), a dockable panel available in the editor, Montage and Master Section:
  - Load several reference tracks (commercial masters or earlier versions) into a list; each keeps its own cue points and loop range
  - Instant, gapless A/B switch between your mix and the selected reference (one key), staying in sync by position or jumping to the reference's matching cue
  - Automatic loudness matching (integrated or short-term LUFS) so the comparison is fair; manual offset available
  - Option to route the reference around the Master Section, so you hear it clean while your audio goes through the chain
  - Side-by-side meters and spectrum overlay for mix vs reference
  - A/B/C/D snapshots of Master Section settings, switchable and comparable the same way
  - Blind compare mode that hides which is which until you pick
- P2 DDP image export and verification, CD burning (via cdrdao/libburn)
- P2 Offline loudness and spec compliance report (PDF/HTML)

### 2.4b Analyze menu (like WaveLab's Analyze tab)
- P1 **Audio File Comparator**: compare two files (or a file against its processed version), produce a difference file, and add markers wherever they differ; good for checking that a render or transcode did only what you expected
- P1 **3D Frequency Analysis**: a rotatable 3D waterfall of frequency over time for a selection, with adjustable FFT size, range and colors; exportable as an image
- P1 **Global Analysis**: scans the file or selection and reports peaks, loudness, pitch, DC offset, clipping, zero crossings, silence and errors, each with clickable/keyboard-navigable results that jump to the spot and can create markers
- P1 **Loudness**: integrated, short-term and momentary loudness, LRA and true peak over time as a graph, compared against a chosen target (streaming, CD, EBU R128, ATSC A/85), with a printable report
- P1 **Visual Analysis**: graphs of level, loudness, spectral centroid and stereo width over time to spot problems at a glance
- P1 All analyses run in the background, work on files, selections or batch jobs, and export as CSV/HTML/PDF

### 2.5 Rendering and transcoding (WaveLab-grade)
- P1 One render engine shared by Save As, Master Section render, Montage export and batch, so every path has the same options
- P1 Multi-format render in a single pass: one source to e.g. 24/96 WAV, 16/44.1 FLAC, MP3 320, AAC 256 and Opus at once, each with its own settings and naming
- P1 Per-output chain: high-quality sample rate conversion, bit-depth reduction with selectable dither and noise shaping, optional loudness normalization or true-peak limiting per target (streaming vs CD vs broadcast)
- P1 Codec preview: hear the encoded result in real time (with a "difference" mode to hear only what the codec removes) before rendering
- P1 Inter-sample/true-peak check after encoding, with a warning if the lossy file clips
- P1 Metadata templates (ID3v2, Vorbis comments, MP4 tags, BWF/iXML, cover art) applied per output, with variables such as track number and title
- P1 Naming schemes with variables (`{title}_{rate}_{bits}.{ext}`), output folder rules, overwrite policy
- P2 Render regions/markers as separate files, render Montage tracks individually, background render queue while you keep editing

| Format | Encoder / library | Notes |
|---|---|---|
| WAV / BWF / RF64, AIFF, W64, CAF | libsndfile | RF64 for files over 4 GB |
| FLAC | libFLAC | compression level, verification |
| MP3 | LAME (libmp3lame) | CBR/VBR/ABR, joint stereo |
| Ogg Vorbis, Opus | libvorbis, libopus | Opus at 48 kHz with automatic SRC |
| AAC / M4A, ALAC | FFmpeg (libavcodec) native AAC and ALAC | optional fdk-aac when the user's build has it (non-free license) |
| WavPack | libwavpack | lossless and hybrid |
| Monkey's Audio (APE), TTA, OptimFROG decode | MAC SDK (BSD), FFmpeg | lossless archives |
| MP2, AC-3, E-AC-3, DTS core, WMA v1/v2, AMR-NB/WB, Speex, GSM, G.711/G.722/G.726, ADPCM variants | FFmpeg (libavcodec), opencore-amr, libspeex | broadcast, telephony, legacy and game formats |
| Musepack, Sun AU, VOC, IRCAM, NIST Sphere, raw PCM (any layout/endianness) | libsndfile, libmpcenc | |
| DSD: DSF, DSDIFF (DFF) | own writer with PCM to DSD modulator | DSD64/128/256 |
| Matroska audio (MKA), WebM audio, MP4/MOV audio-only | FFmpeg | container export with chapters from markers |

**Export goal: every audio format that has an encoder available on Linux.** The export dialog lists formats from whichever encoders are present at runtime (FFmpeg exposes dozens more), so new FFmpeg builds add formats without app changes. Formats with no Linux encoder at all (TAK, MQA) are import-only or unsupported, and that is shown in the dialog rather than hidden.

### 2.6 Batch and automation
Modeled on WaveLab's Batch Processor: a saved batch is a reusable document (file list + chain + outputs) you can rerun anytime.
- P1 Input: add files, folders (recursive, with filters), or a whole Montage; drag in or add by keyboard
- P1 Processing chain: any built-in processors and plugins (all formats) in order, plus offline-only processes (normalize to peak/LUFS, DC removal, trim silence, fade in/out, auto heal / declick, resample, channel conversion, time/pitch)
- P1 Outputs: one or many per job, using the shared render engine (formats, SRC, dither, naming, metadata templates from 2.5)
- P1 Analysis jobs that write no audio: loudness, true peak, clipping and silence scans, exported as CSV/HTML/PDF reports
- P1 Conditional steps, e.g. only limit if true peak > -1 dBTP, only resample if rate differs, skip files that already meet a target
- P1 Runs in parallel on all CPU cores, in the background while you keep editing; progress, pause/resume, per-file log, retry failed files
- P1 Batch rename (from metadata, patterns, numbering) and batch metadata write as their own job types
- P1 Batch presets saved and shared between editor, Master Section and batch
- P2 Watch folders: drop files in, processed files come out
- P2 Headless CLI (`audio-editor --batch job.xml`) for servers and CI, using the same batch file
- P2 Scripting API (Lua) for custom steps and macros

### 2.6b Metadata editor and writer (Audition-style, multiple schemas)
Modeled on Audition's Metadata panel: one editor shows every schema in the file side by side, and you choose which ones get written on save or render.
- P1 Read and write all common schemas:

| Schema | Where it lives | Library |
|---|---|---|
| XMP (Dublin Core, xmpDM dynamic media, rights, custom namespaces) | WAV, AIFF, MP3, FLAC, MP4, sidecar `.xmp` | Exempi (BSD) |
| BWF `bext` (description, originator, time reference, UMID, loudness fields) | WAV / RF64 | own chunk reader/writer |
| iXML (scene, take, track names, production metadata) | WAV | own reader/writer |
| Cart chunk (AES46 broadcast: cue, start/end dates, talk-up timers) | WAV | own reader/writer |
| RIFF INFO (IART, INAM, ICMT...) | WAV | own reader/writer |
| ID3v1, ID3v2.3, ID3v2.4 (incl. cover art, lyrics, chapters) | MP3, WAV, AIFF | TagLib (LGPL) |
| Vorbis comments + FLAC pictures | FLAC, Ogg, Opus | TagLib |
| MP4 / iTunes atoms | M4A, AAC, ALAC | TagLib |
| APEv2 | WavPack, APE | TagLib |
| CD-Text, ISRC, UPC/EAN | Montage / DDP | own |
| Markers and regions (cue, labl, Audition XMP markers) | WAV, XMP | own |

- P1 Field mapping between schemas (e.g. title to `INAM`, `TIT2`, `TITLE` and `dc:title` at once) with a "sync all schemas" option and visible conflicts when they disagree
- P1 Choose which schemas are written per file type and per render output; option to strip all metadata
- P1 Templates with variables (date, filename, track number, loudness values measured at render), applied in the editor, renders and batch
- P1 Auto-fill BWF loudness fields (LUFS, LRA, true peak) from measurement at render
- P1 Raw view: every chunk/frame/atom in the file, including unknown ones, preserved on save unless deleted
- P1 Batch metadata write and import/export as CSV or JSON for spreadsheet editing of whole albums
- P2 Custom XMP namespaces and field definitions for house/broadcast standards
- P2 Online lookup (MusicBrainz) for album and track info

### 2.7 Plugin hosting (all Linux plugin formats)
| Format | Priority | How |
|---|---|---|
| LV2 (incl. plugin GUIs) | P0 | JUCE built-in LV2 host |
| VST3 | P0 | JUCE built-in VST3 host |
| CLAP | P1 | clap + clap-helpers (MIT); JUCE has no native CLAP host |
| LADSPA | P1 | JUCE built-in LADSPA host |
| DSSI | P2 | small custom host (LADSPA plus MIDI and GUI extension) |
| VST2 (Linux .so) | P2 | needs VST2 headers; Steinberg no longer licenses the SDK, so this uses a clean-room header and is optional at build time |
| Windows VST2/VST3/CLAP | P1 | via yabridge, which exposes them as native Linux plugins, so no extra host code; tested and documented |

- P1 Plugin scanning in a separate process so a crashing plugin does not kill the app; scan paths follow each format's Linux conventions (`~/.lv2`, `~/.vst3`, `~/.clap`, `LADSPA_PATH`, `/usr/lib/*`)
- P1 Unified plugin browser: search, favorites, tags, format filter, all keyboard navigable
- P2 Per-instance sandboxing for unstable plugins

### 2.8 Keyboard-first (hard requirement)
Every menu item and every process (edit, effect, analysis, render, batch) is a registered command with a keybinding and is reachable without a mouse.
- P0 One central command registry: menus, toolbar buttons, context menus and shortcuts all invoke the same commands, so nothing exists that only a mouse can reach
- P0 Every command ships with a default binding; the full set is shown in a searchable shortcut reference (F1)
- P0 Menu bar opens with Alt / F10 and Alt+mnemonic letters, navigates with arrow keys, and shows each item's shortcut
- P0 Command palette (Ctrl+Shift+P) to fuzzy-search and run any command by name
- P0 Keyboard focus moves between panels with Tab/Shift+Tab and F6, with a visible focus ring; dialogs work fully with Tab, Enter and Esc
- P0 Editing by keyboard: move the cursor by sample, zero crossing, marker or second; extend selections with Shift; zoom with +/-; transport on Space, Enter, Home, End, J/K/L shuttle
- P1 Keymap editor with conflict detection, import/export, saved under `~/.config/<app>/keymap.xml` (XDG)
- P1 Linux-friendly defaults: avoids keys GNOME/KDE grab globally (Super combos, Ctrl+Alt+arrows, Ctrl+Alt+T); optional presets matching Audition and WaveLab bindings
- P1 Screen reader support through JUCE accessibility (AT-SPI on Linux)
- P0 Desktop shortcut check (done 2026-10-17): on Hyprland, read `hyprctl binds -j` and flag PHINYX shortcuts the desktop already takes (startup notice and a Note column in Keyboard Shortcuts); never use the Super key
- P2 Chorded / leader-key sequences and recordable keyboard macros

### 2.8b Themes and skins (REAPER-style, third-party themes)
- P0 Every color, font and metric in the UI comes from a theme; nothing is hard-coded in widgets (built on a data-driven JUCE LookAndFeel)
- P1 Ships with several themes (dark, light, high contrast, a classic WaveLab-like and an Audition-like layout palette)
- P1 **Theme packages that third parties can make and share**, like REAPER's `.ReaperTheme` / `.ReaperThemeZip`: a single `.aetheme` file (zip) containing
  - `theme.json` with color, font, size and spacing values, and waveform/spectrogram color maps
  - optional images (PNG/SVG) for buttons, knobs, meters, icons, backgrounds, with 9-slice scaling for HiDPI
  - optional fonts and a `layout` section for panel arrangement
- P1 Install by drag and drop or a menu command; themes live in `~/.local/share/audio-editor/themes` (XDG) and can also be installed system wide
- P1 Theme manager: preview, switch instantly without restart, set per-workspace themes
- P1 Built-in theme tweaker that edits values live and saves as a new theme, so users can make themes without writing files by hand
- P1 Published, versioned theme format spec and a starter template so others can build themes; validation with clear errors and fallback to defaults for missing values
- P0 Waveform colour picker (done 2026-10-17): Options > Waveform Colors (`Ctrl+Shift+W`) sets personal waveform, background, selection, cursor, centre line, marker and region colours on top of any theme
- P0 Focus follows mouse inside the app, like Hyprland (done 2026-10-17, View menu, on by default)
- P0 Window opacity control (done 2026-10-17): slider in the status bar and View > Window Opacity, `Ctrl+Alt+-` / `Ctrl+Alt+=`, remembered; works with the compositor's own opacity
- P2 Scriptable layout (WALTER-style in REAPER) for moving and resizing elements, and a theme repository/browser

### 2.9 MIDI controller mapping (Ableton-style)
- P1 **Mapping mode on `Ctrl+M`** (like Ableton): the UI dims and every mappable control and command is highlighted; select a control (click, or Tab/arrows and Enter by keyboard), move a knob/fader or press a button on the hardware, and it is mapped; `Ctrl+M` again to exit
- P1 Anything can be mapped: every command in the command registry (transport, markers, A/B switch, auto heal, render...) and every continuous parameter (Master Section, plugin parameters, zoom, scrub, levels)
- P1 Messages supported: Note, CC (absolute, relative 2's complement/sign-bit/binary offset for endless encoders), 14-bit CC, NRPN, Program Change, Pitch Bend; with min/max range, inversion, takeover modes (pickup/scale) to avoid jumps
- P1 Works with any MIDI hardware over ALSA MIDI, JACK/PipeWire MIDI and USB class-compliant devices; hot-plug safe
- P1 Mapping browser listing every mapping, editable by keyboard; mappings saved per device as profiles you can import/export
- P1 MIDI feedback to the device (LEDs, motorized faders, displays) so the hardware reflects the app's state
- P2 Mackie Control / HUI protocol support and OSC mapping

### 2.9b Focusrite Scarlett / Clarett / Vocaster integration
Built on Geoffrey Bennett's Linux work on GitHub: the kernel mixer drivers (Scarlett 1st Gen driver, Scarlett2 driver in Linux 6.7+, FCP driver in Linux 6.14+ with the `fcp-support` daemon for large 4th Gen units) and the `alsa-scarlett-gui` control panel, which together are the Linux equivalent of Focusrite Control.
- Supported, matching that project: Scarlett 1st Gen 6i6 and up, 2nd Gen 6i6/18i8/18i20, 3rd Gen Solo to 18i20, 4th Gen Solo to 18i20, Clarett USB and Clarett+ 2Pre/4Pre/8Pre, Vocaster One/Two
- P1 Auto-detects a connected interface (ALSA card name/USB ID) and shows an **Interface panel** inside the app (dockable, pops out to a tile), talking to the same ALSA mixer controls the driver exposes through alsa-lib's control API, so no extra driver is needed
- P1 Recording-relevant controls in the app: input gain/autogain, phantom power, pad, Air, inst/line, direct monitor, speaker switching (Main/Alt), monitor dim/mute, output trims
- P1 Routing and matrix mixer presets: load and save the interface's routing as part of a session or the Master Section preset, so a mastering setup (e.g. outputs for monitors vs reference headphones) comes back with the project
- P1 Hardware level meters shown next to the app's own meters where the driver exposes them
- P1 All interface controls are commands, so they get keyboard shortcuts and MIDI mappings like everything else
- P1 Driver/kernel check: tells the user if their kernel is too old for their model, or if `fcp-support` is needed, with a link to the setup docs
- P2 "Open full control panel" launches `alsa-scarlett-gui` for anything not covered in-app (firmware updates, full routing matrix)
- Licensing: `alsa-scarlett-gui` is GPL-3.0 (to confirm when we start this work); GPLv3 code can be combined with this app's AGPLv3 license, but the plan is to talk to the ALSA controls directly and only reuse code where it saves real effort, with credit

### 2.9b2 Resident Audio T2 / T4 integration (requested 2026-10-18)
Same shape as 2.9b, built on the Resident Audio T2 and T4 Linux drivers, which live in their own repository (to be finished and published separately from PHINYX). PHINYX auto-detects the unit, shows the Interface panel with its recording controls, routing presets and hardware meters where the driver exposes them, makes every control a command, and checks the driver is installed, linking to that project's setup steps if not. The T2 driver still needs testing on real hardware (owner, 2026-10-18). GUI (requested 2026-10-18: "make a GUI for the interface, maybe based on the one that Resident Audio had"): a control panel laid out like Resident Audio's own (its mixer and routing layout, per-channel controls, meters), redrawn in the PHINYX theme rather than copying its artwork or logos, which belong to Resident Audio. It ships in the driver project as a standalone app for people without PHINYX, and the same panel opens inside PHINYX as its Interface panel. Needs screenshots or a manual of the original panel to follow its layout. Driver status from the owner's handoff notes (2026-10-14, saved at /mnt/project-files/resident-audio/t4-driver-handoff.md; owner asked 2026-10-18 for "it and a T2 version integrated like the ALSA Scarlett and Clarett Linux driver and gui interface"): the T4 is a VIA Envy24HT (VT1724, PCI 1412:1724, subsystem 3033:5241) behind Thunderbolt, made by Ego Systems (ESI). Linux's `snd_ice1724` already drives that chip but rejects this board (unknown model ID 0x33304152, non-standard EEPROM). The vendor startup sequence (GPIO serial bus to the main control chip, CCS values, optional AK4384 DAC setup) was extracted from the Mac and Windows drivers. Update from the owner (2026-10-18): those notes are older than the latest work; a later Claude session got sound working on the owner's Omarchy laptop with their T4, so the working driver code lives on that laptop and needs bringing into the driver repository. More T4 units and a T2 are on order (eBay) for wider testing. Driver order (step 1 is mostly done, pending the code): (1) T4 board file for `snd_ice1724` with built-in EEPROM data and the vendor init, built out of tree, first target stereo playback at 44.1 kHz; owner loads it and reports dmesg, aplay -l and a low-volume speaker-test; then all outputs, inputs, rates and MIDI. (2) T2: the same investigation starting from the T2 drivers in `reference-drivers/` (whether it uses the same chip is not known yet). (3) Mixer controls exposed through ALSA, so the standalone GUI and PHINYX's Interface panel read and set them the same way as the Scarlett driver. Ground rules from the notes: never write the EEPROM, never run or change the vendor binaries, nothing installed into the system or boot config, the owner runs every module load. Needed: the working driver code from the Omarchy laptop, and the T2 when it arrives.

### 2.9c Library and browser (Ableton-style, searches everything)
One browser panel (dockable, pops out to its own tile) with a single search field that finds **anything in the app**, like Ableton Live's browser.
- P1 Sidebar categories: Collections/Favorites, Audio Files, Plugins (by format and vendor), Stock Effects, Master Section Presets, Plugin Presets, User Presets, Batch Jobs, Render/Transcode Presets, Metadata Templates, Themes, MIDI Mappings, Reference Tracks, Recent, plus user-added Places (folders, external drives, network shares)
- P1 **Global search** across every category at once, as you type: names, tags, plugin vendor/format/type, preset contents, file metadata (title, artist, BWF description, markers), sample rate, length, loudness
- P1 Results grouped by category with filters (`type:plugin`, `format:lv2`, `rate:96k`, `lufs:<-14` style tokens, also as clickable chips)
- P1 Fully keyboard driven: `Ctrl+F` jumps to search, arrows move, `Enter` loads (opens a file, inserts a plugin or stock effect into the selected Master Section slot, applies a preset), `Right` expands, `Ctrl+D` favorites
- P1 **Audio preview**: arrow through files and hear them instantly, synced to tempo-free playback, loudness-matched optionally, through a separate preview output
- P1 Drag (or a key) from the browser into the Master Section, the A/B Reference panel, a batch job or the Montage
- P1 Background indexer: SQLite + FTS5 full-text index of files, plugins and presets, updated live with inotify when folders change; fast on large sample libraries
- P1 Collections: user colors/tags (like Ableton's colored collections) for any item, saved per user
- P2 Similar-sound search (find files that sound like this one) using audio feature vectors

### 2.10 Workflow and UI
- P0 HiDPI, resizable dockable panels
- P1 Workspaces (Edit, Restore, Master, Montage) like Audition's workspaces
- P0 Any panel pops out to its own window (one command per panel, plus a generic "pop out focused panel"), designed for tiling window managers like Hyprland
- P1 Waveform/spectral thumbnails in the browser (see 2.9c)
- P0 Autosave and crash recovery (requested 2026-10-18 after a crash lost unsaved edits). Done: see "Where things stand". Later: keep the undo history too

---

### 2.11 Multi-channel and Ambisonics (requirement, added 2026-10-17)
Multi-channel means one file with many channels (5.1, 7.1, 7.1.4, Ambisonics), not many tracks.

| Feature | Priority |
|---|---|
| Channel-agnostic engine: playback, meters, Master Section and render for any channel count (today they assume stereo) | P0 |
| Speaker layouts read and written from WAV `WAVE_FORMAT_EXTENSIBLE` channel masks, plus BWF/ADM and CAF; layout shown per lane (L, R, C, LFE, Ls, Rs...) | P0 |
| Per-channel solo/mute, channel routing to device outputs, downmix monitoring on stereo hardware (ITU-R BS.775 coefficients) | P0 |
| Multi-channel loudness (BS.1770 channel weights, LFE excluded) and per-channel true peak | P0 |
| Channel tools: extract, insert, swap, reorder, split to mono files, merge mono files into one file | P1 |
| Ambisonics, first and higher order (FOA to 7th), AmbiX (ACN/SN3D) native, FuMa import and export | P1 |
| Ambisonic encoder (place mono or stereo sources), rotate (yaw, pitch, roll), mirror, order change | P1 |
| Ambisonic decoders: binaural for headphones (SOFA HRTF files via libmysofa) and to speaker layouts (AllRAD) | P1 |
| Upmix stereo to 5.1, 7.1, 7.1.4 or Ambisonics: STFT-based primary/ambient split, centre extraction, ambience to surrounds, adjustable width and LFE crossover, fold-down check | P1 |
| Mono to stereo ("stereoize"): complementary comb or all-pass decorrelation, frequency-dependent spread, Haas option, a mono-compatibility meter that proves L+R still sums back cleanly | P1 |
| Surround and Ambisonic visualisation: per-channel meters, energy map (sphere) for Ambisonics, phase scope per pair | P2 |
| Host multi-channel plugins (IEM Plugin Suite, SPARTA, ambiX on Linux) with full bus layouts in the Master Section | P1 |

Building blocks: the IEM Plugin Suite (GPLv3, Linux VST3/LV2) works as hosted plugins straight away. Spatial_Audio_Framework (SAF, ISC-licensed core) and libmysofa (BSD) can supply built-in encoders, decoders and HRTF loading.

## 3. Recommended Linux tech stack

### Pick: **C++20 + JUCE 8** engine, with PipeWire/JACK/ALSA audio via JUCE, CMake build

The UI is **Qt 6** (decided; see "UI toolkit decision for Wayland" above). JUCE provides only the audio engine: devices, codecs, DSP and plugin hosting.

Why this is the pick:
- **Audio-first framework.** JUCE gives audio device I/O (ALSA, JACK, and PipeWire through its JACK/ALSA layers), sample-accurate buffers, file codecs, DSP modules, and plugin **hosting for VST3, LV2 and (via extensions) CLAP** out of the box. Building those from scratch in other stacks is months of work.
- **Proven for exactly this kind of app.** Waveform editors, DAWs and mastering tools are routinely built on JUCE; real-time thread rules, lock-free FIFOs and plugin sandboxing patterns are well documented.
- **Native performance** for spectral rendering, FFT-heavy restoration and Montage playback, without a GC or browser in the audio path.
- **Licensing:** JUCE 8 is dual-licensed (AGPLv3 or commercial). If the app is open source under AGPLv3, it is free; if closed source, a JUCE commercial license is needed. Decide this early.

### Supporting libraries

| Need | Library |
|---|---|
| File formats and codecs | libsndfile, libFLAC, LAME, libvorbis, libopus, libwavpack, FFmpeg (AAC/ALAC and catch-all import) |
| Sample rate conversion | libsamplerate or r8brain-free-src |
| Time/pitch | Rubber Band (GPL / commercial) |
| FFT | FFTW3 or pffft (pffft is BSD and simpler to ship) |
| Loudness | libebur128 (MIT) for BS.1770 / R128 |
| CLAP hosting | clap + clap-helpers (MIT) |
| LV2 / VST3 / LADSPA hosting | JUCE built-in hosts |
| DSSI, VST2 (optional) | small custom hosts |
| Restoration / ML | own DSP first; ONNX Runtime later for optional ML models |
| Metadata | TagLib (LGPL), Exempi for XMP (BSD), own BWF/iXML/cart chunk code |
| Project/session files | SQLite or JSON (nlohmann/json) |
| Scripting (P2) | Lua (sol2) |
| Tests | Catch2, plus golden-file audio tests |

### Target platform: Arch Linux, Omarchy, Hyprland
The app is optimized first for **Arch Linux**, including **Omarchy** (DHH's Arch + Hyprland setup), running under the **Hyprland** Wayland compositor. Other distros and desktops still work but are second priority.
- **Wayland first.** Hyprland is a Wayland-only compositor. JUCE 8.0.15 has no native Wayland backend (checked in its source: X11 only), so an all-JUCE UI runs through XWayland. See the UI toolkit decision below.
- **Tiling-friendly windows.** Every panel (Master Section, meters, A/B Reference, spectrogram, marker list, analysis windows) can pop out to its own top-level window with a stable window class/app-id (e.g. `audio-editor.master-section`), so Hyprland tiles it and users can write `windowrule`s for size, workspace, float or pin. Popping back in docks it again. Layouts remember which panels are popped out.
- **Keyboard defaults that don't fight Hyprland.** Omarchy and most Hyprland configs put window management on `Super`; the app never binds `Super` by default.
- **Omarchy theme sync (P1).** An optional theme that follows Omarchy's current system theme colors, so the app matches the desktop when the user switches themes. (Based on Omarchy keeping its active theme in a config folder; to verify against the current Omarchy release.)
- **Portals.** File dialogs and screenshots go through xdg-desktop-portal (portal-hyprland / portal-gtk), not toolkit-specific dialogs.
- **Fractional scaling.** Must look sharp at Hyprland's fractional scales (1.25, 1.5); tested on HiDPI.
- **Packaging priority:** AUR first (`audio-editor` and `audio-editor-git` PKGBUILDs), then Flatpak and AppImage for other distros.
- **Docs:** a Hyprland/Omarchy setup page with example `windowrule`s, PipeWire low-latency settings (`pipewire-jack`, quantum), and realtime privileges (`realtime-privileges` group on Arch).

### UI toolkit decision for Wayland (decided 2026-10-17: Qt 6 UI + JUCE engine)
- **Chosen: Qt 6 UI + JUCE headless engine.** Qt 6 is native Wayland, sharp at fractional scales, has mature keyboard mnemonics, AT-SPI accessibility, style sheets for skinning, and dock widgets that float out as real windows (KDDockWidgets). JUCE 8 now ships `juce_audio_processors_headless` (plugin hosting for VST3/LV2/LADSPA with no GUI dependency), so the engine keeps JUCE's audio devices, codecs, DSP and plugin hosting. Plugin editor windows run in a separate helper process under XWayland (Linux plugin GUIs are X11 anyway), which also isolates plugin crashes.
- **Rejected: all-JUCE (the first scaffold).** Simpler, one framework, already builds and runs. Runs via XWayland on Hyprland; tiling works, but it is not native Wayland and needs `xwayland { force_zero_scaling = true }` plus app-side scaling to stay sharp.

### Audio backend on Linux
- **PipeWire** is the default on Arch (and Omarchy), Fedora, Ubuntu and Debian, and it exposes a JACK-compatible API. Target **JACK API through PipeWire** as the primary path (low latency, routing with other apps), with **direct ALSA** as a fallback for minimal systems.
- Real-time thread scheduling via rtkit / `SCHED_FIFO`, documented in a setup guide.

### Packaging
- **AUR** first: `audio-editor` (release) and `audio-editor-git` PKGBUILDs.
- **Flatpak** (Flathub) for other distros, with the PipeWire socket and portals.
- **AppImage** for a single-file download.
- deb/rpm later, ideally community maintained.

### Alternatives considered

| Stack | Verdict |
|---|---|
| **Rust** (cpal, nih-plug, egui/iced/Slint) | Great safety and growing audio ecosystem, but plugin *hosting* (VST3/LV2/CLAP), waveform UI widgets and mature editor toolkits are still thin. Good for DSP modules later via FFI. |
| **Qt 6 (C++) + own audio engine** | Excellent UI toolkit and native Linux feel, but you would hand-build device I/O, plugin hosting and DSP that JUCE already provides. Strong option if UI richness outweighs audio plumbing. Qt is LGPL/commercial. |
| **Electron / Tauri + Web Audio** | Fast to prototype UI, but Web Audio is not suitable for sample-accurate destructive editing of large files, low-latency monitoring, or native plugin hosting. Rejected for the core. |
| **GTK 4 (C / Rust)** | Fits GNOME, but same audio-plumbing gap as Qt and fewer pro-audio precedents. |

**Hybrid worth noting:** JUCE engine + Qt UI is possible but doubles the framework surface. Start pure JUCE; revisit only if the UI becomes the bottleneck.

---

## 3b. Reusing Audacity 4 code

Checked against Audacity's `master` branch (Audacity 4) on 2026-10-17.

**License: allowed.** Audacity is GPLv3, and many files are "GPLv2 or later". Both can be combined with this app's AGPLv3 license. Conditions: keep Audacity's copyright headers, note our changes, and keep reused code under GPL terms. We can't use the Audacity name or logo.

**How Audacity 4 is built.** It's a Qt 6 / QML app on MuseScore's "Muse" framework (`src/`, `muse/`), wrapping about 80 libraries carried over from Audacity 3 (`au3/libraries/au3-*`) plus older effect code in `au3/src/effects`. Nearly every `au3-*` library still links **wxBase**, the non-GUI part of wxWidgets (strings, files), and many depend on Audacity's track and project model.

**Approach: port algorithms and lift self-contained libraries. Don't fork the app.** The app shell, track model and UI are built around multitrack editing, which this app doesn't need.

| Our feature | Audacity source | Reuse how |
|---|---|---|
| FFT, spectral analysis | `au3-fft` (pffft based), `au3-wave-track-fft` | Lift `au3-fft`; small, only needs wxBase for strings |
| Spectrogram display (2.2) | `src/spectrogram` (`SpectrumCache`, spectrogram painter and settings) | Port the cache and painter logic into our spectral view; the QML views are a reference if we pick Qt |
| Noise reduction, click removal (2.2) | `au3/src/effects/NoiseReduction.cpp`, `ClickRemoval.cpp` | Port the DSP onto our buffers; strip track and UI code |
| Loudness / normalize (2.4, 2.5) | `au3/src/effects/Loudness.cpp`, `Normalize.cpp`, `Amplify.cpp` | Port; check results against libebur128 |
| Compressor, limiter (2.4) | `au3-dynamic-range-processor` (`CompressorProcessor`, `SimpleCompressor`), `Limiter.cpp` | Lift `au3-dynamic-range-processor`; it depends only on components, strings and utility, and has tests |
| EQ (2.4) | `au3-builtin-effects/Equalization*`, `ScienFilterBase`, `BassTrebleBase` | Port filter design and curves |
| Time stretch, pitch shift (2.1) | `au3-time-and-pitch`, `au3-stretching-sequence` | Lift `au3-time-and-pitch` (pffft + wxBase); a GPL alternative to Rubber Band |
| Resampling, reverb, math | `au3-math` (soxr, libSoX reverb) | Use soxr directly; port the pieces we need |
| Silence/clipping detection (2.3b auto markers, 2.4b) | `TruncSilenceBase`, `FindClippingBase`, `PlotSpectrumBase` | Port detection loops for auto markers and Global Analysis |
| Plugin hosting (2.7) | `au3-lv2`, `au3-ladspa`, `au3-vst3`, `au3-vst` | Reference only; JUCE already hosts LV2, VST3 and LADSPA. Useful for LV2 edge cases and the VST2 fallback |
| Import/export, metadata (2.5, 2.6b) | `au3-file-formats` (libsndfile facade), `au3-import-export`, `au3-tags` | Reference only; we use libsndfile, FFmpeg and TagLib directly |
| Nyquist effects | `au3-nyquist-effects`, `share/nyquist-plug-ins` | P2: optional Nyquist plugin support for Audacity's large user plugin library |

**Not reused:** the app shell (`src/app`, `src/appshell`), project and track model (`au3-project*`, `au3-track`, `au3-wave-track`), UI and theme code (`au3-shuttlegui`, `au3-theme`, `au3-wx-*`), cloud and crash reporting.

**Practical rules**
- Put copied code under `third_party/audacity/` with its original headers, plus a `NOTICE` listing source paths and the Audacity commit used.
- For lifted libraries, accept wxBase as a build dependency (on Arch it's in the `wxwidgets-common` package; no wx GUI needed), or replace its few string/file calls with std/JUCE equivalents when porting.
- Every ported DSP algorithm gets a golden-file test comparing our output with Audacity's on the same input.
- If the UI decision is Qt 6, Audacity 4's QML components (spectrogram view, effect dialogs) are also usable as GPL reference designs.



```
+-------------------------- UI (JUCE components, OpenGL/Vulkan-accelerated) -------------------------+
|  Waveform / Spectral view  |  Montage (A/B)       |  Meters  |  Master Section  |  Batch  |  Browser |
+--------------------------------------------------------------------------------------------------- +
                 |  commands (undoable)                       ^ lock-free meter/peak data
                 v                                            |
+---------------------- Document model --------------------------------------------------------------+
|  AudioDocument (paged sample store on disk, peak/FFT caches)   Project (tracks, clips, automation)   |
|  Undo stack (command pattern, copy-on-write blocks)                                                  |
+--------------------------------------------------------------------------------------------------- +
                 |                                            ^
                 v                                            |
+---------------------- Engine ----------------------------------------------------------------------+
|  Real-time playback graph  |  Offline render graph  |  DSP library (EQ, dyn, restoration, SRC, dither)|
|  Plugin host (out-of-process scanner; in-process or sandboxed instances)                             |
+--------------------------------------------------------------------------------------------------- +
                 |
        JACK API (PipeWire) / ALSA
```

Key design choices:
- **Paged, disk-backed sample storage** so multi-GB files open instantly and edits are copy-on-write block swaps (makes undo cheap). This is the Sound Forge "feels instant" trick.
- **Precomputed multi-resolution peak files and spectrogram tiles**, built in background threads.
- **One processing-chain format** reused by Master Section, clip/track FX and the batch processor.
- **Strict real-time rules:** no allocation, locks or I/O on the audio thread; UI talks to the engine through lock-free queues.
- **Headless core** (engine + document model as a library) so the CLI/batch mode and tests reuse it.

---

## 5. Phased roadmap

### Phase 0: Foundations
- Repo, CMake, CI (GitHub Actions in an Arch Linux container, plus Ubuntu), clang-tidy, sanitizers
- App shell with central command registry, keymap, command palette, menus (done in the scaffold)
- Audio device setup dialog (JACK/PipeWire + ALSA)
- License decision: done, AGPLv3
- UI toolkit decision for Wayland: done, Qt 6
- AUR PKGBUILD

**Exit:** app opens on Hyprland, plays a WAV through PipeWire, everything reachable by keyboard, CI builds it on Arch.

### Phase 1: MVP single-file editor (Sound Forge-level basics)
- Paged audio document, open/save core formats
- Waveform view with fast zoom, selection
- Markers and regions: add on the fly, marker list, jump next/previous
- Data-driven theming foundation (all colors/fonts from a theme file), dark and light themes
- Cut/copy/paste/trim, fades, normalize (peak + LUFS), gain, reverse, DC removal
- Undo/redo history
- Loudness + true-peak + peak meters
- WaveLab-style Master Section (effects slots with solo/bypass/reorder, final effects and dither, presets) with dithered render
- Pop-out panels as separate windows that Hyprland tiles, starting with the Master Section
- Spectral Frequency Display (view, zoom, FFT settings) with split waveform view
- LV2 and VST3 hosting

**Exit:** a user can open a file, fix levels, cut it, run it through LV2 plugins, hit a loudness target and export. This is the first public alpha.

### Phase 1.5: Multi-channel foundation
- Recording and capture like OBS: desktop audio, any input, one app (2.1b P1). Done, except several sources at once.
- Make playback, meters, Master Section and render work for any channel count, with speaker layouts and downmix monitoring (2.11 P0). Done.
- Built-in Stereoize (mono to stereo), as a menu command and a live Device Chain effect. Done.
- Done early because every later feature (repair, Montage, export) depends on how channels are handled

### Phase 2: Repair lab (Audition-level restoration)
- Spectral selection tools (marquee, lasso, brush, harmonics) and spectral edits (done, Parts A and C)
- Auto Heal (done, Part A) and Spot Healing Brush (done, Part C)
- Noise print reduction, DeClick, DeClip, DeHum, DeEss (done, Part B)
- Diagnostics scan panel
- CLAP and LADSPA hosting, yabridge support, out-of-process scanner (done, Part E3)
- SRC and bit-depth conversion
- Metadata editor with all schemas, field mapping, templates

**Exit:** podcast/dialogue cleanup and vinyl restoration workflows work end to end.

### Phase 3: Mastering desk (WaveLab-level)
- Ambisonics (encode, rotate, binaural and speaker decode) and stereo upmix to surround or Ambisonics (2.11 P1)
- Built-in mastering processors: EQ, true-peak limiter, dither (other effects via plugins, see 2.4 decision)
- Stock plugins from ZL Audio (requested 2026-10-17): ZL Equalizer 2 (dynamic EQ), ZL Spectrum Equalizer, ZL Compressor, ZL Limiter and ZL Splitter, built into PHINYX with their own windows and listed under Built-in in Add Plugin. Licence check: each repository (github.com/ZL-Audio) is AGPLv3, the same as PHINYX, so the code may be included and changed as long as PHINYX stays AGPLv3 and its source stays public; their dependencies (JUCE, Highway, nlopt, Material Symbols, Inter, pamplejuce) are permissive or AGPL-compatible. The ZL Audio and per-plugin logos are excluded from the licence, so they are left out and replaced with PHINYX styling; the names stay as credit ("ZL Compressor by ZL Audio, zsliu98, AGPLv3") in the About box, the plugin's card and README, with each upstream LICENSE kept beside its code. Approach: add each repo as a git submodule pinned to a release, build its processor and JUCE editor into the app as an `AudioPluginInstance` like Stereoize, and show the editor in the same window as other plugins (it is already a JUCE UI). Risks: they need Clang 17+ and their own JUCE version (align with ours or bump ours), and Highway adds build time; keep an option to build without them. Ask upstream for a friendly heads-up before release (not required by the licence).
- Stock plugin: Wolf Shaper (requested 2026-10-17), a waveshaper with a spline graph editor, from github.com/wolf-plugins/wolf-shaper. Licence check: GPLv3, which may be combined with AGPLv3 code (section 13 of both licences allows it), so it can ship inside PHINYX with its source; credit "Wolf Shaper by Patrick Desaulniers and contributors, GPLv3" in the About box, its card and README, and keep its LICENSE beside it. It is built on DPF (not JUCE), whose CLAP build already runs in our CLAP host with its own window (the DPF plugins MVerb and Dragonfly were tested in Part E3). Approach: add it as a git submodule, build its CLAP target as part of PHINYX, install it to PHINYX's own plugin folder, and list it under Built-in in Add Plugin so it needs no scan.
- Stock plugins: Dusk Audio (requested 2026-10-17), github.com/dusk-audio/dusk-audio-plugins. Licence check: GPLv3 (combinable with AGPLv3 as for Wolf Shaper); its DAF framework is a DPF fork under ISC. Take the mastering-relevant ones: 4K EQ, Multi-Q (with its analyzer), Multi-Comp (seven modes plus multiband), TapeMachine 2, Tape Echo 2, DuskVerb (pre-release, add when it is released) and Spectrum Analyzer; leave out Chord Analyzer, GrooveMind and DuskAmp (MIDI and guitar tools). The JUCE ones are built in like ZL Audio's, the DAF ones as bundled CLAP like Wolf Shaper. Credit "Dusk Audio, GPLv3".
- Stock plugins: WetDelay, WetReverb and WetEQ by yonie (wetvst.com, requested 2026-10-17). Licence check: the site and press say the full C++ source is MIT, which may be included in an AGPLv3 program with its notice kept. They are written straight on the Steinberg VST3 SDK, so the plan is to build their VST3s with PHINYX and load them through the existing VST3 host. Open item: the source repository is not linked from wetvst.com; find it (github.com/yonie) and confirm the LICENSE file before starting. Credit "Wet plugins by yonie, MIT".
- Pitch editing from PitchNet (requested 2026-10-17), github.com/SessionLoops/PitchNet, a piano-roll vocal pitch editor (neural pitch detection, note segmentation, HiFiGAN resynthesis; JUCE; itself derived from HachiTune). Licence check: the code is AGPLv3, so it can be included with credit to SessionLoops and HachiTune. Two open items before it ships: the neural models it needs (PC-NSF-HiFiGAN and GAME from OpenVPI, RMVPE, FCPE) carry their own licences that PitchNet does not state, and some OpenVPI vocoder releases are non-commercial (CC BY-NC-SA), which may limit how PHINYX can be distributed; and it pulls in ONNX Runtime (MIT), a large dependency. Since it is an editor rather than an effect, it fits as a PHINYX Pitch Edit view on the selection rather than a Device Chain plugin. Plan it after the effect plugins, once the model licences are confirmed.
- Stock meter: ROSS VU by Rick Rossati (requested 2026-10-17), github.com/RickRossati/ross-vu, an analog-style VU meter with standard ballistics (second-order, 300 ms to 99 %, about 1 % overshoot, average rectification), a CAL setting from -20 to -8 dBFS for 0 VU and a 4x oversampled true-peak lamp. Licence check: GPL-3.0-or-later, combinable with AGPLv3; DPF is ISC and its Liberation Sans font is OFL, so all of it can ship with credit ("ROSS VU by Rick Rossati, GPLv3"). Built on DPF and NanoVG with CLAP among its formats, so it is bundled as CLAP like Wolf Shaper and runs in the existing CLAP host. Besides the Device Chain, offer it in the Meters panel as a VU view alongside the peak and loudness meters, following the same ballistics.
- Stock analyzer: Open Audio Analyzer by Jonas Grunau (requested 2026-10-17), github.com/JonasGrunau/open_audio_analyzer: 14 meter modules (LUFS, true peak, VU, spectrum, spectrogram, phase scope, histogram...) on a canvas, live or on files. Licence check: the C11 measurement engine and core packages are GPL-3.0-or-later and the JUCE plugin is AGPL-3.0-or-later, both fine to include; its fonts (Inter, Google Sans Code) are OFL. Credit "Open Audio Analyzer by Jonas Grunau". Its app UI is Flutter, which cannot live inside PHINYX's Qt window, so: (1) build its VST3 plugin with PHINYX and offer it in the Device Chain like the other stock plugins, and (2) use its C engine directly for an Analyzer panel in PHINYX, drawn in the PHINYX theme, that can sit in the Meters area and also measure a whole file offline (it overlaps the planned Analyze menu and Loudness tools, so it replaces building those meters from scratch). Its tablet mirroring is left out.
- Stock clipper: LSP Clipper and LSP Multiband Clipper (requested 2026-10-17), from github.com/lsp-plugins (each plugin has its own repository, e.g. lsp-plugins-clipper and lsp-plugins-mb-clipper). Licence check: LGPL-3.0-or-later (Debian's copyright file says LGPL-3+, with some ISC parts), which may be combined with AGPLv3; credit "LSP Clipper by Vladimir Sadovnikov and the LSP project, LGPLv3". Already works today: with `lsp-plugins` installed, PHINYX scans the CLAP Clipper Mono/Stereo and Multiband Clipper Mono/Stereo and loads them with their own windows (Part E3 tested LSP Limiter Stereo the same way). As a stock plugin: build just these two from their repositories as CLAP with PHINYX, bundle them under Built-in, and put them in the shared frame.
- Stock EQ: ToneShiftEQ (requested 2026-10-17), from github.com/brummer10/ToneShiftEQ. A 12-band parametric EQ with zero-latency FFT (minimum-phase) and biquad engines, per-band dynamic EQ (threshold, ratio, direction, side-chain), high and low pass, HF Fade, APO EQ profile import and export, and a live spectrum with phase overlay. Licence check: BSD-3-Clause (Copyright (c) 2024 brummer), which may be combined with AGPLv3; keep its copyright notice and disclaimer in the source and in the About/credits, credit "ToneShiftEQ by brummer10, BSD-3-Clause", and do not use the author's name to promote PHINYX. Its GUI uses brummer10's libxputty (X11/Cairo, a git submodule, licensed 0BSD, which needs no notice at all). Already works today as a CLAP or VST3 plugin once built with `make clap`. As a stock plugin: build its CLAP with PHINYX, bundle it under Built-in, and show its own curve and spectrum view in the shared frame.
- One look for all stock plugins (requested 2026-10-17: "a unified stock plugin GUI without sacrificing the graphical aesthetics"). Every stock plugin opens in the same PHINYX frame: a header bar (name, credit line, on/off, A/B compare, preset menu, undo/redo, oversampling where offered), input and output meters on the sides, and a footer of Ableton-style knobs that also drive the keyboard and the Device Chain card. The plugin's own visual heart stays in the middle and is kept, not flattened: ZL's EQ curves and spectrum, Wolf Shaper's spline graph, Dusk's analyzers and tape reels. How: the JUCE plugins (ZL Audio, Dusk's JUCE ones, PitchNet) get a shared PHINYX `LookAndFeel` that takes its colours, fonts and knob drawing from the active PHINYX theme, so they follow Dark, Light and user themes; the DPF/DAF ones (Wolf Shaper, Dusk's DAF ones) and the Wet VST3s keep their own drawing inside the frame, with small patches to their colour constants to match the theme where the licence allows (all of them do). Build order: the frame first with the built-in Stereoize, then each plugin family as it is added.
- Spectrum, phase correlation, vectorscope
- Analyze menu: Audio File Comparator, 3D Frequency, Global, Loudness and Visual Analysis
- Loudness target presets, Match Loudness
- A/B Reference section with loudness-matched reference tracks and settings snapshots
- Shared render engine with multi-format single-pass transcoding, codec preview and metadata templates
- Batch processor: multi-output jobs, analysis reports, conditional steps, parallel background runs

**Exit:** v1.0. Master a single and an EP, batch-convert a library.

### Phase 4: Montage, themes and control
- Lightweight Montage (no mixer): two or more tracks for A/B referencing, album assembly with gaps, ISRC, CD-Text
- Theme package format, theme manager, tweaker and published spec for third-party themes
- MIDI mapping mode (Ctrl+M), mapping browser, device profiles, MIDI feedback
- Scarlett / Clarett / Vocaster interface panel
- Library and browser with global search across files, plugins, stock effects and presets
- Omarchy theme sync
- DDP export, recording with punch-in

### Phase 5: Power features
- Scripting API (Lua), headless CLI, watch folders
- Pitch/time stretch, ML denoise/dialogue isolation (ONNX)
- Control surface mapping, compliance reports
- DSSI and optional VST2 hosting

### Extra (after the main work): Sampler view (requested 2026-10-18)
"A sampler function that would turn any audio file into a simple sampler like Ableton's Simpler. Not trying to create a full sampling engine... the main workspace of the editor could instantly invoke another view... with all those functions. That would entail MIDI integration." Look: an adaptation of Simpler's minimalist layout in the PHINYX theme, not a copy of Ableton's artwork.
- One key binding sends the file in the editor (or its selection) to a new Sampler tab beside the file tabs (owner, 2026-10-18: "a key binding could take the audio file in the editor to another window (a tab) and the sampler window would appear"). The editor tab stays as it was; Ctrl+Tab switches between them, and the sampler can be popped out into its own window like the panels. The sampler keeps its own copy of the sound, with a command to refresh it from the editor after more edits.
- Sample page: the waveform with start, end and loop markers; modes Classic (sustain loop), 1-Shot (plays through, with fade in/out) and Slice (slices by transient, beat division, regions/markers or manually, each slice on its own key, from C1 up); Gain, Start, Loop, Length, Fade, Loop on/off, Snap to zero crossings, Voices (1 to 32), Retrigger, Warp (time-stretch to tempo with the stretcher PHINYX already uses) with Complex/Beats/Tones.
- Controls page: filter (low/high/band pass and notch, 12/24 dB, frequency, resonance, envelope and key/velocity amounts) with its curve drawn, LFO (shape, rate in Hz or synced, depth to volume, pitch, pan and filter), amplitude ADSR drawn as a curve with draggable points, pitch envelope, pan and spread, transpose, detune, glide, velocity to volume.
- MIDI: any MIDI keyboard or controller through ALSA/JACK MIDI (JUCE's MIDI input), with note on/off, velocity, pitch bend and sustain; the computer keyboard plays it too (Ableton-style A to K row, Z/X for octaves) while the view is open; every control is a command, so the MIDI-mapping plan (2.9) covers it.
- Purpose (owner, 2026-10-18): "a sound design tool that leverages the power of a full-fledged audio editor with a sampler function", and "the plugin chain would work". So the sampler's output runs through the Device Chain (all stock and third-party plugins) and the Master Section like file playback does, and the editor's tools (repair, Nyquist, effects, Stereoize) shape the sound before it reaches the sampler; a "Resample" command records what the sampler plays, through the chain, into a new file tab.
- Save and export: the sampler's settings are kept with the file's session; export the slices as files or as a SFZ instrument (sample plus a text map), which other samplers and DAWs open.
- Source to reuse: Ninjas2 by Clearly Broken Software (github.com/clearly-broken-software/ninjas2), a sample slicer on DPF with aubio onset detection. Licence check: GPL-3.0 (aubio is GPL-3.0 too), combinable with AGPLv3 with credit. Its last change was in 2023, so take its slicing and slice-to-key mapping code as a starting point rather than bundling the plugin; Classic and 1-Shot playback, the filter, LFO and envelopes are written for PHINYX (the filter and envelope maths are small).
- Order: Classic and 1-Shot with ADSR and MIDI first, then Slice (from Ninjas2), then filter, LFO and Warp, then SFZ export.

### Extra (after the main work): Visualizer
Requested 2026-10-17 as an extra once PHINYX's main phases are done. A Visualizer window (its own tile, title "Visualizer - PHINYX", class phinyx) fed by whatever PHINYX plays, after the Master Section, so it shows the processed sound. Effort estimates for one developer:
- Bars and spectrum in the style of **cava** (github.com/karlstav/cava, MIT as far as checked): its `cavacore` library is made for embedding. Feed it PHINYX's playback blocks and draw with Qt, in theme colours. About 2 to 3 days.
- MilkDrop presets like **butterchurn** (github.com/jberg/butterchurn, MIT): butterchurn is JavaScript/WebGL, so using it directly means bundling a web engine (Qt WebEngine, a large dependency). The native route is **projectM** (the C++ MilkDrop engine butterchurn follows, LGPL-2.1, packaged on Arch as `projectm`): an OpenGL view in the window plus a preset browser and keyboard control. About 1 to 2 weeks. Preferred over butterchurn.
- Shader visuals like **GLava** (github.com/jarcode-foss/glava, GPLv3, no longer maintained): reuse its GLSL modules (bars, radial, graph, wave) in the same OpenGL view as projectM rather than its X11 window code. About 3 to 5 days on top of the projectM view.
Order: cava-style first (small, useful), then projectM, then GLava's shaders if still wanted. Total about 3 to 4 weeks for all three.

---

## 6. Risks and open questions

1. **License model.** Decided 2026-10-17: **open source under AGPLv3**, using JUCE under its AGPLv3 license. GPL-family dependencies such as Rubber Band are compatible.
2. **Restoration quality.** Noise reduction and spectral healing are the hardest DSP to make competitive; budget research time or plan ML models in Phase 5.
3. **Plugin stability.** Third-party plugins crash. Out-of-process scanning is in scope; full per-instance sandboxing is a later decision.
4. **Distro fragmentation.** Flatpak sandbox vs PipeWire/JACK access and real-time priority needs testing on Ubuntu, Fedora and Arch early.
5. **Scope.** Three flagship apps is a lot. Phase 1 must stay a tight single-file editor. No mixer is planned, which keeps the Montage small.

## 7. Immediate next steps
- ~~Decide license model~~ Done: AGPLv3 open source
- ~~Scaffold Phase 0~~ Done: app shell, command registry, keymap, command palette, Arch CI, PKGBUILD
- ~~Decide the interface toolkit~~ Done: Qt 6 UI + JUCE engine
- ~~Push to a repository~~ Done: github.com/phinyx852Hz/audio-editor
- ~~Start Phase 1~~ Done: editing, undo, markers, meters, Master Section, spectrogram view (chunked copy-on-write audio document)
- Test on the owner's Omarchy machine, then finish the open Phase 1 items
- First Audacity reuse candidates: `au3-fft`, `au3-dynamic-range-processor`, NoiseReduction (see 3b)
