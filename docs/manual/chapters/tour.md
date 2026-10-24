# A Tour of the Window

PHI opens to one window. In its middle is the editor, where the open file is drawn as a waveform. Panels can sit around it, on the left, the right and along the bottom, and any of them can pop out into a window of its own. This chapter names every part, so the rest of the manual can refer to them.

![The PHI window: menus and transport at the top, the overview and editor in the middle, panels around them, and the Device Chain along the bottom.](images/tour-workspace.png)

## The menu bar

[]{.ix t="Menus"}Across the top are PHI's menus. Each holds commands of one kind, and each opens from the keyboard with `Alt` and its underlined letter, or with `F10` and the arrow keys.

| Menu | Holds |
|---|---|
| File | Open, save, record, export, metadata, recent files |
| Edit | Undo, cut, copy, paste, selection, snapping |
| Process | Gain, normalize, fades, reverse, stereo tools, sample type |
| Repair | Spectral repair, restoration, diagnostics, pitch correction |
| Effect | Nyquist effects and generators |
| Spatial | Binaural, channel layouts, ambisonics, speaker arrays |
| Warp | Beat grid, tempo, stretching, warp markers, quantize |
| Markers | Markers, regions, export at markers |
| Album | Albums of songs and CD images |
| Analyze | Loudness, tempo and key, beats and onsets, Vamp plugins |
| Tools | Batch processor, macros, Nyquist prompt, plug-in manager |
| Master | Master Section, Device Chain, references, Master Assistant |
| Transport | Play, stop, loop, record and moving the cursor |
| View | Panels, zoom, spectral view, rulers, meters, opacity |
| Visualizer | The Visualizer, MilkDrop, Sonify, the web page behind PHI |
| Mixer | The eight-track mixer, patchbay, projects, Mix Assistant |
| Sample | Simplest, the Slicer, Granular, Retro Sampler, MIDI inputs |
| Synth | The synthesizers |
| Tracker | Tracker1 and Tracker2 |
| Performance | Video recording, live streaming, the clip launcher |
| Options | Audio device, theme, colours, shortcuts |
| Help | About PHI, the keyboard reference |

## The transport bar

[]{.ix t="Transport bar"}Below the menus is the transport: Go to Start, Play/Pause, Stop, Go to End, Record and Loop, then the time and the tempo.

![The transport bar.](images/tour-transport.png)

| Button | Key | Does |
|---|---|---|
| Go to Start | `Home` | Moves the cursor to the start of the file |
| Play/Pause | `Space` | Plays from the cursor; pressed again, pauses where it is |
| Stop | `K` | Stops and goes back to where playback started |
| Go to End | `End` | Moves the cursor to the end |
| Record | `Ctrl+R` | Opens the Record window (see [Recording](#recording)) |
| Loop | `Ctrl+L` | Plays the selection, or the whole file, over and over |

Each button shows its shortcut when you hover over it. The time reads hours, minutes, seconds and milliseconds (`00:00:00.000`). The **BPM** box beside it shows the file's tempo; type a new one and press `Enter` to set it.

## Tabs

[]{.ix t="Tabs"}Each open file has a tab above the editor, with its own undo history, selection and zoom. `Ctrl+Tab` and `Ctrl+Shift+Tab` (or `Ctrl+Page Down` and `Ctrl+Page Up`) move between tabs, and `Ctrl+W` closes one. Copy and paste work between tabs. See [Files, Tabs and Recovery](#files).

## The overview {#overview}

[]{.ix t="Overview"}Above the editor, the overview shows the whole file. A box marks the part the editor shows.

- Drag across the overview to zoom the editor to that stretch.
- Drag the box to move it, or drag an edge of the box to zoom.
- Turn the wheel over it to zoom around the mouse.
- Double-click to move the cursor there.

`Ctrl+Alt+O` shows or hides the overview.

## The editor

[]{.ix t="Editor"}[]{.ix t="Waveform"}The editor draws the open file, one lane per channel, each labelled with its speaker (L, R, C, LFE and so on). A time ruler above it follows the zoom from hours down to microseconds; click it to move the cursor. The ruler can count time, bars and beats, or both (see [Tempo, Warping and Quantize](#warp)).

Two small controls float over the waveform:

- the **gain knob**, near the top centre, which turns the selection up or down while you listen;
- the **fade handles**, the small squares in the top corners, which draw a fade in or out.

[Selecting and Editing](#editing) explains both. View > Gain Knob (`Ctrl+Alt+K`) hides the knob.

The editor can also show a spectrogram instead of the waveform, or a blend of the two: see [The Spectral View](#spectral).

## The file details

[]{.ix t="Status bar"}Along the bottom of the editor are the cursor position and file length, the selection, the format (sample rate and channels), what playback is doing, and the audio device. The digits are steady, so nothing shifts as the numbers change. While you adjust something, such as the gain knob, its value shows here too.

## The panels

[]{.ix t="Panels"}Panels hold tools you want beside the waveform. Each opens with its own key and closes again with the same key or `Ctrl+W` while it has the keyboard.

| Panel | Key | See |
|---|---|---|
| Find | `Ctrl+F` | [The Find Panel and the Selector](#find) |
| Meters | `Ctrl+Shift+L` | [Meters and Analysis](#meters) |
| Markers | `Ctrl+Shift+M` | [Markers and Regions](#markers) |
| History | `Ctrl+H` | [Undo and the History panel](#history) |
| Diagnostics | `Ctrl+Shift+D` | [Diagnostics](#diagnostics) |
| References | `Ctrl+Alt+Shift+R` | [References and the Master Assistant](#references-assistant) |
| Master Section | `Ctrl+Shift+E` | [The Master Section and Device Chain](#master-section) |
| Device Chain | `Ctrl+Shift+C` | [The Device Chain](#device-chain) |
| Quantize | `Shift+Q` | [The Quantize panel](#quantize-panel) |

### Moving and resizing panels

- Drag the line between two panels to resize them. Where three or four panels meet, the cursor becomes a four-way arrow: drag there to resize both ways at once, as in Audition.
- `Ctrl+Alt+P` pops the panel with the keyboard out into its own window, and docks it back. On Hyprland, a popped-out panel tiles like any other window. All of PHI's shortcuts work in it, and `Esc` closes it and returns to the waveform (unless you are typing in a field).
- PHI remembers the layout. View > Reset Panel Layout (`Ctrl+Alt+0`) puts every panel back where it started.

### Which panel has the keyboard

[]{.ix t="Focus"}The panel that has the keyboard gets the same coloured frame as the waveform. With **Focus Follows Mouse** (View menu, on by default), pointing at a panel gives it the keyboard, as on Hyprland; it never takes the keyboard away from a text field or a dialog. [Moving between panels](#panel-focus) explains the keys.

## The Device Chain

Along the bottom of the window is the Device Chain: the plugins of the Master Section as cards, from left to right into the Master card with its gain and dither. Every sound you play passes through it. See [The Master Section and Device Chain](#master-section).

## The corner

At the bottom right are the **CPU meter** and the **Opacity** slider.

- **CPU** is how much of the processor PHI uses, all its threads together, updated every second (100 % is one core, as `top` counts it). **Audio** is how much of each audio block's time the sound takes to make. It turns red from 80 %; near 100 % the sound drops out. View > CPU Meter hides them.
- **Opacity** makes the whole window see-through, to show your desktop or the Visualizer behind it. `Ctrl+Alt+-` and `Ctrl+Alt+=` change it. A Hyprland opacity rule still applies on top.
