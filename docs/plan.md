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

## Where things stand (2026-10-17)
- **Plan:** this document, covering every requirement above.
- **Code:** github.com/phinyx852Hz/audio-editor. Phase 0 foundation, rebuilt on Qt 6 (native Wayland) with a JUCE audio engine underneath. It opens a file, shows the waveform, plays it through ALSA or JACK/PipeWire, and every action is a remappable command: command palette (`Ctrl+Shift+P` / `F1`), menus by `F10` / `Alt`+letter, shortcut editor with conflict detection (`Ctrl+Shift+K`). Tested by keyboard on a virtual X11 display; not yet run on a real Wayland/Hyprland session or with a real sound device.
- **Also in place:** Arch + Ubuntu CI, an AUR-style PKGBUILD (`phinyx-git`), README.
- **Decided:** AGPLv3; Qt 6 UI + JUCE engine; name PHINYX.
- **Next:** Phase 1, the first usable editor.

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
- P0 Fade in/out with curve shapes, crossfades at edit points
- P0 Normalize (peak and LUFS), gain, invert, reverse, DC offset removal
- P0 Unlimited undo/redo with a history list
- P0 Markers and regions (full spec in 2.3b)
- P1 Pencil tool for redrawing samples
- P1 Channel ops: swap, extract, mix down, mid/side convert
- P1 Sample rate conversion (high-quality SoX/r8brain-style) and bit-depth conversion with dither (TPDF, noise-shaped)
- P1 Loop point and sampler (smpl/inst chunk) editing
- P2 Pitch/time stretching (Rubber Band library)

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
- P1 Spectrum analyzer, phase correlation meter, goniometer/vectorscope
- P1 Built-in mastering processors: parametric EQ (linear and minimum phase), multiband compressor, limiter (true-peak aware), stereo width, de-esser
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

---

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

### Phase 2: Repair lab (Audition-level restoration)
- Spectral selection tools (marquee, lasso, brush, harmonics) and spectral edits
- Auto Heal and Spot Healing Brush
- Noise print reduction, DeClick, DeClip, DeHum, DeEss
- Diagnostics scan panel
- CLAP and LADSPA hosting, yabridge support, out-of-process scanner
- SRC and bit-depth conversion
- Metadata editor with all schemas, field mapping, templates

**Exit:** podcast/dialogue cleanup and vinyl restoration workflows work end to end.

### Phase 3: Mastering desk (WaveLab-level)
- Built-in mastering processors (EQ, multiband comp, true-peak limiter, width)
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
- Start Phase 1: editing, undo, markers, meters, Master Section, spectrogram view
- Prototype the paged audio document and waveform view, since they decide how the whole app feels
- First Audacity reuse candidates: `au3-fft`, `au3-dynamic-range-processor`, NoiseReduction (see 3b)
