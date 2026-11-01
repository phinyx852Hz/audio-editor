# The Master Section and Device Chain

[]{.ix t="Master Section"}The Master Section is where mastering happens. Like WaveLab's, it is a chain of up to eight plugins, then a master gain and final dither, and everything you play passes through it: the open file, references, the album. You hear its effect as you work, and when you are happy, you render it into the file or export through it.

PHI shows the same chain in two places:

- the **Master Section panel** (`Ctrl+Shift+E`), a list of slots, good for the keyboard;
- the **Device Chain** (`Ctrl+Shift+C`), cards along the bottom of the window, good for turning knobs, as in Ableton Live and Bitwig.

![The Device Chain: Φ EQ, Φ Space, the True Peak Limiter and a Prism, each with its Dry/Wet.](images/device-chain.png)

## Finding plugins

[]{.ix t="Plugins!scanning"}[]{.ix t="VST3"}[]{.ix t="CLAP"}[]{.ix t="LV2"}[]{.ix t="LADSPA"}PHI hosts VST3, CLAP, LV2 and LADSPA effects. Master > Scan Plugins (`Ctrl+Alt+Shift+S`) looks for them in the usual folders. Each plugin file is checked by a separate helper copy of PHI, so a plugin that crashes or hangs while being scanned is skipped from then on and never takes the editor down.

[]{.ix t="Manage Plug-ins"}Tools > Manage Plug-ins lists every plugin found, with its maker, format and file, and a search box. Untick a plugin to hide it when you add plugins. Files that crashed or hung during the scan are listed with **Try Again**, and **Rescan** looks again.

### Windows plugins {#yabridge}

[]{.ix t="yabridge"}[]{.ix t="Windows plugins"}Windows plugins work through [yabridge](https://github.com/robbert-vdh/yabridge). After `yabridgectl sync`, the VST3 and CLAP copies it makes in `~/.vst3/yabridge` and `~/.clap/yabridge` are found by the next scan. VST2 plugins are not supported.

If a plugin stops working while you use it (a Windows plugin whose Wine side closes makes yabridge report "write: Broken pipe", for example), PHI switches just that plugin off, lets the sound pass through, marks its card in red and keeps running. Press **Reload** on the card (or `E`) to load it again with its last settings.

## Adding a plugin

Master > Add Plugin (`Ctrl+Alt+E`), or *Add plugin* in the Device Chain, opens a list of every plugin, with PHI's own under **Built-in**. Type to search, and press `Enter`. The plugin goes into the first free slot, and its own window opens.

From the Find panel, `Enter` on a plugin loads it the same way.

## The Device Chain {#device-chain}

[]{.ix t="Device Chain"}Each card has:

- an **on/off** switch and a **solo** button (hear only that plugin while playing);
- up to eight of the plugin's **knobs**, in two rows;
- a **Dry/Wet** knob, whatever the plugin, for parallel processing;
- a **level bar**;
- **Edit** (all its parameters) and **Window** (the plugin's own window).

Knobs are drawn like Ableton Live's, with the value filled in along an arc (the master gain fills from 0 dB). Drag up or down to turn one, with `Shift` for fine steps, or double-click to type a value.

| Keys | Does |
|---|---|
| `Left` / `Right` | Picks a card |
| `Up` / `Down` | Picks a knob |
| `+` / `-` | Turns it (`Shift`, or `Page Up` / `Page Down`, for bigger steps) |
| `F2`, or typing a number | Sets an exact value (`Enter` sets it, `Esc` cancels) |
| `Enter` | Edits all the parameters |
| `Insert` | Adds a plugin after the card |
| `Del` | Removes it |
| `B` | Bypasses it |
| `S` | Solos it |
| `E` | Opens or closes its window |
| `Ctrl+Left` / `Ctrl+Right` | Moves it |
| `Shift+Left` / `Shift+Right` | Picks several cards |
| `Ctrl+G` / `Ctrl+Shift+G` | Groups the picked cards into a Prism, and ungroups one |
| `Esc` | Back to the waveform |

Drag a card by its name to move it with the mouse.

[]{.ix t="Dry/Wet"}[]{.ix t="Parallel processing"}**Dry/Wet.** Every slot has its own Dry/Wet, even when the plugin has none, so parallel compression or saturation needs no Prism. The dry sound is delayed by the plugin's latency, so the two line up. Double-click or `F2` and type a percentage, or `wet` or `dry`. The mix is saved with the chain.

## The Master Section panel

[]{.ix t="Master Section!panel"}The panel lists the eight slots. In the list:

| Keys | Does |
|---|---|
| `Enter` | Opens a parameter editor made for the keyboard: type to filter, arrows to adjust |
| `E` | Opens or closes the plugin's own window |
| `B` | Bypasses the slot |
| `Del` | Removes it |
| `Ctrl+Up` / `Ctrl+Down` | Moves it |
| `Ctrl+Shift+B` | Bypasses the whole Master Section |

## Plugin windows

[]{.ix t="Plugin windows"}A plugin's own window opens when you add it, and with its card's Window button or `E`. It opens floating at a standard size, centred, with the plugin scaled to about two thirds of the screen, on Hyprland too; `Super+F` takes it full screen. Popped-out panels such as Simplest and the Slicer float at a standard size as well. A window can then be any size: a plugin of fixed size is asked to draw itself bigger or smaller to fit (from half to four times its size, which most JUCE-made CLAP and VST3 plugins can do) and sits centred in the rest.

With a plugin window focused, `Esc` or `Ctrl+W` closes it and returns the keys to the chain, and `Ctrl+Tab` / `Ctrl+Shift+Tab` go to the next or previous plugin window. Plugins drawn with their own toolkit may keep those keys; the card's Window button, `E` or your desktop's close key still work. If the desktop force-closes a plugin window, PHI keeps running.

[]{.ix t="Window opacity!plugin windows"}Plugin windows follow PHI's [Opacity](#opacity) slider, so they go see-through with the rest of PHI; the instruments keep their own opacity bar. Hyprland ignores the usual X11 opacity setting, so there PHI sets the opacity, and the floating, through Hyprland itself.

[]{.ix t="Φ effects!window"}**The Φ effects** open in a window of their own instead: see [The Φ window](#phi-window).

::: note
Plugin windows use X11 (through XWayland on a Wayland desktop), since that is what Linux plugins draw with.
:::

## Prisms {#prisms}

[]{.ix t="Prisms"}A Prism is a device in the chain that splits the sound into **rays**, rows of plugins side by side, keeps them in time and joins them again, like light through a prism. A ray can hold a Prism too, so Prisms nest.

![A Prism popped out of its card, split into mid and side.](images/prism-editor.png)

| Split | Rays | What each ray hears | How they join |
|---|---|---|---|
| Parallel | Up to 8 | Everything | Added (an empty ray is the dry sound, for parallel compression) |
| Bands | Up to 4 | Ray 1 the lowest band, ray 2 the next… | Added; the bands add back flat at every frequency |
| Mid/Side | 2 | Ray 1 the mid, ray 2 the side | Back to left and right |
| Left/Right | 2 | Ray 1 the left, ray 2 the right | Back to left and right |

Bands uses Linkwitz-Riley crossovers (24 dB per octave), so an empty Prism gives back the same level at every frequency. Each ray has a gain (−60 to +12 dB), mute and solo. Rays with less latency are delayed to line up with the slowest, so switching a plugin never shifts the sound.

**Making one.** Add *Prism* from *Built-in*, or pick cards with `Shift+Left`/`Right` and group them with `Ctrl+G`. `Ctrl+Shift+G` puts a one-ray Prism's plugins back in the chain.

**Editing.** Docked, a Prism is one card with eight **macro** knobs and a bar per ray. `Enter`, `E` or Open pops it out over the window to edit its rays; `Esc` docks it back. Inside, `Left`/`Right` pick a device, `Up`/`Down` a ray, `Insert` adds a plugin, `Ctrl+T` adds a ray, `M` and `S` mute and solo the ray, and `Ctrl+Up`/`Down` move a device to the next ray. Plugins drag between rays.

**Macros.** Press **Map**, turn or click a macro, then click any knob on a device in the rays. Right-click a macro to set each mapping's range or remove it, or to **Learn MIDI Controller**: move a knob on your controller and the macro follows it from then on.

## Modulators {#modulators}

[]{.ix t="Modulators"}[]{.ix t="modulator rack"}The Modulators panel (*Master ▸ Modulators*) is a rack of sources that move parameters of the plugins in the Device Chain, after the Modulators panel of Cubase. It sits in the bottom zone with the Device Chain. **Add Modulator** offers:

| Modulator | Controls | What it gives |
|---|---|---|
| LFO | Rate, Shape (Sine, Triangle, Saw Up, Saw Down, Square, Φ Drift), Morph, Fold, Phase | A wave; Morph blends towards the next shape (Cubase's Morph LFO), Fold folds it back on itself (Wavefold LFO); Φ Drift is three sines at rate × 1, φ and φ² |
| Envelope Follower | Attack, Release, Gain, Floor | The level of what goes into the chain |
| Step Modulator | Rate, Steps (1 to 16), Glide, sixteen steps | A step sequence, starting from the golden sequence |
| Random | Rate, Smooth, Déjà vu, Length, Seed | New values Rate times a second; Déjà vu (after Mutable Instruments' Marbles) replays the last Length values in order at its middle, and in another order past it |
| Sample & Hold | Source, Rate, Seed | Another modulator (or noise) caught Rate times a second |
| Shaper | Source, Bend, Steps, Invert | Another modulator bent, stepped and turned over |
| Attack-Decay | Attack, Decay, Threshold, Retrigger, Curve | An envelope fired when the input rises past Threshold, or Retrigger times a second |
| Macro Knob | Value | A knob of your own, to move several destinations at once |
| Crossfader | Source A, Source B, Position, Curve | A blend of two modulators |
| Φ Flow | Rate, Ratio, Mode, Shape, Output, Jitter, Seed | Flow from [Φ Modulation](#phi-modulation): a sequence that never repeats, or Periodic, Fibonacci or Hybrid |

Choose a modulator to see its controls and a trace of what it gives. **Add Destination** lists the plugins in the chain's slots and their parameters; each destination moves between its **Low** (the modulator at 0) and **High** (at 1), as a Prism macro does, and High below Low turns it over. To move something inside a Prism, make one of its macros the destination. A destination whose plugin is replaced or removed shows *(gone)* and does nothing.

Modulators move parameters during playback and in renders alike, every 128 samples or less, and start from the top at every render, so a render comes out the same each time. Nothing in the rack is random in the rolled sense: Random, Sample & Hold's noise and Φ Flow's Hybrid are worked out from their Seed. The rack is saved with the chain.

## Final dither {#final-dither}

[]{.ix t="Dither!final"}Dither is built into the Master card, after the master gain, so it never takes a plugin slot: **Off**, **16**, **20** or **24** bits (the Dither and Noise knobs on the Master card, or the Final dither row in the Master Section panel). It applies to playback and to Render in Place. Exports and batch outputs round to their own bit depth as their last step instead, with the same choice of noise in the Export dialog; Convert Sample Type offers it too.

[]{.ix t="Dither!types"}[]{.ix t="Noise shaping"}The noise, from none to the most shaped:

| Noise | What it does | Use it |
|---|---|---|
| Plain rounding | Rounds to the nearest step, no dither. Quiet detail below the last bit is lost, and fades turn grainy | To compare |
| Truncate | Cuts the extra bits off: distortion and a DC offset | To hear why not |
| RPDF | Rectangular noise of ±½ step. Removes the distortion, but the noise rises and falls with the music | To compare |
| TPDF | Triangular noise of ±1 step: no distortion and a steady, even hiss | The safe choice for any delivery, and for audio that will be processed again |
| TPDF high-pass | TPDF with its noise tilted towards the treble, less in the bass | 24-bit masters, and whenever you don't use noise shaping |
| TPDF, gentle shaping | TPDF with 2nd-order shaping: the hiss rises steadily towards the top | Monitoring |
| TPDF, Lipshitz 5th order | Shaped to the ear's sensitivity at low levels (Lipshitz, Wannamaker and Vanderkooy, 1991) | 16-bit masters at 44.1 or 48 kHz |
| TPDF, Wannamaker 9th order | The deepest shaping: about 24 dB less noise at 3 to 4 kHz, where hearing is keenest, and more above 15 kHz (Wannamaker, 1992) | 16-bit masters at 44.1 or 48 kHz |

The shaped kinds are made for 16 bits at 44.1 kHz, and work at 48 kHz too. At 88.2 kHz and above, use TPDF. Shaped dither makes the noise much quieter, but only as long as nothing changes the level afterwards. A volume control, a player's sample-rate converter or a streaming service's loudness normalisation can lift the shaped hiss into hearing, so TPDF travels better through today's digital playback. For a 24-bit master, use high-passed TPDF without shaping; truncating at 24 bits can still be heard as a grain on very quiet passages.

[]{.ix t="Auto-blanking"}**Auto-blanking:** after about 11 ms of digital silence (exact zeros), every kind stops adding noise, so gaps between songs and the ends of fades stay truly silent.

Every save to 16 or 24 bits adds TPDF dither, unless the audio is already on that bit depth's steps. Inside PHI everything is 32-bit floating point, so nothing is rounded between plugins and no dither is needed there.

## Latency and timing

[]{.ix t="Latency compensation"}Plugin latency is compensated. While you listen, the cursor follows what you hear, behind by the plugins' and the sound card's delay, and switching a plugin off, soloing one or bypassing the section keeps the sound in time.

## Rendering {#render}

[]{.ix t="Render in Place"}[]{.ix t="Render to New Tab"}

| Command | Keys | Does |
|---|---|---|
| Render in Place | `Ctrl+Shift+Enter` | Applies the chain to the selection or the whole file, as an undoable edit, then bypasses the section so the effect is not heard twice |
| Render to New Tab | `Ctrl+Alt+Shift+Enter` | Puts the rendered selection or file in a new tab, with its markers and tags, and leaves this file and the chain as they are |

To write files through the chain, use [Export](#export) with *Through the Master Section*. The chain is remembered between sessions.
