# Welcome to PHI

PHI is an audio editor, a mastering suite and a small DAW for Linux. It brings together the best of three classic editors, Adobe Audition, Sound Forge and WaveLab, and adds an eight-track mixer, a clip launcher, samplers, synthesizers, trackers, spatial audio, a visualizer and a video recorder of its own. It runs natively on Wayland and is made for Arch Linux, Omarchy and the Hyprland desktop, though it runs on any modern Linux desktop.

![PHI with the Find panel on the left, the meters on the right and the Device Chain along the bottom.](images/tour-workspace.png)

## What PHI is for

**Editing.** PHI is first a wave editor in the tradition of Sound Forge and Audition. Open a file, select, cut, fade, normalize, repair a click or a cough, remove hum and noise, stretch to a tempo, and save. Every edit can be undone, and every file opens in its own tab.

**Mastering.** Like WaveLab, PHI has a Master Section: a chain of plugins every sound plays through, with final dither and a gain stage, and Render to apply it. Around it are loudness meters to broadcast standards, reference tracks to compare against, a Master Assistant that tunes your chain towards a target, export to every format FFmpeg knows, a batch processor and an album editor that writes CD images.

**Producing and performing.** PHI also makes music. An eight-track mixer plays audio and MIDI clips through instruments; a patchbay routes everything to everything; a clip launcher plays loops in scenes; samplers, synths and two trackers make the sounds; and the Visualizer and the video recorder take the performance to an audience.

**Spatial audio.** Surround up to 9.1.6, binaural placement for headphones, and ambisonics from first to tenth order, with a studio of spatial effects modelled on Envelop for Live.

## What makes PHI different

[]{.ix t="Keyboard-first design"}**It is driven from the keyboard.** Every action in PHI is a command with a shortcut, a place in a menu and an entry in the command palette (`Ctrl+Shift+P`). There is nothing you can do only with the mouse. If you remember one key, make it `F1`: it finds any command by name.

**It belongs on a tiling desktop.** Panels pop out into windows of their own that Hyprland tiles like any other; focus follows the mouse; `F6` and `Alt` with the arrows move between panels the way Hyprland moves between windows. PHI never uses the Super key, which Omarchy keeps for itself, and warns you when one of its shortcuts is already taken by your desktop.

**It is built on the golden ratio.** Its name, its mark and a family of its effects come from φ, the golden ratio. [On Φ](#phi) tells that story.

**It is free.** PHI is free software under the GNU Affero General Public License. Its stock plugins and instruments are free software too, built from their own source with PHI.

## Where to start

- To install PHI, read [Installing PHI](#installation).
- To find your way round the window, read [A Tour of the Window](#tour).
- To learn how to drive it, read [Working from the Keyboard](#keyboard). It is short and it pays for itself in a day.
- If you came from Audition, Sound Forge or WaveLab, skim [Selecting and Editing](#editing) and [The Master Section and Device Chain](#master-section): most things are where you expect, under the same names.
- If you came from Ableton Live or Bitwig, go to [The Find Panel and the Selector](#find), [The Mixer, Patchbay and Projects](#mixer) and [The Clip Launcher](#clip-launcher).

::: hardware
PHI has been developed and tested with virtual and USB audio, but not yet on every audio interface. If something behaves oddly with yours, [Audio devices](#audio-device) explains what to try.
:::
