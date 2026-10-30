# Projects (.phi)

A PHI project is a mix and production session in one file: the mixer's tracks with their
clips and instruments, the strips and master, the patchbay, and which view was in front. It is
saved with Mixer > Save Project (`Ctrl+S` in the mixer, `Ctrl+Shift+S` for Save As), opened with
Mixer > Open Project (`Ctrl+O` in the mixer), with File > Open, by passing it to `phi` on the
command line, or by double-clicking it in a file manager.

PHI starts on a blank project. When it quits and the project has unsaved changes, it asks to
Save, Discard or Cancel (Cancel keeps PHI open). File > New Project (`Ctrl+N`) asks the same.

For crash recovery, the project as it is goes to `last.phi` in `~/.config/phi/mixer/` once a
minute while PHI runs (with the recovery autosave), and a clean quit removes it. If PHI did not
close properly, the next start offers it back: Recover opens it under its own name, still marked
unsaved if it was, with its patchbay, and with the Mixer in front if it was; No removes it.

## The file

JSON, written atomically (`QSaveFile`). A project is a mix file (`.phinyxmix`, version 2) with
more in it, so both open the same way; a mix from before projects keeps the patchbay as it is,
and `Ctrl+S` on one saves a `.phi` beside it, leaving the old file alone.

```json
{
  "phinyxProject": 1,
  "phinyxMix": 2,
  "tempo": 120, "midiFollowsTempo": false, "master": 0, "armed": 0,
  "tracks": [
    {
      "name": "DR", "colour": "#c0392b",
      "trim": 0, "fader": -3.5, "pan": 0, "invertLeft": false, "invertRight": false,
      "mute": false, "solo": false, "transpose": 0, "channels": 65535, "toPreMaster": false,
      "spatial": { "azimuth": -30, "elevation": 0, "spread": 60, "distance": 1 },
      "clips": [ { "kind": "audio", "file": "Audio/kick.wav", "start": 0, "offset": 0,
                   "length": 4.2, "muted": false, "reversed": false } ],
      "instrument": { "kind": "synth", "state": "..." }
    }
  ],
  "preMaster": { "trim": 0, "fader": 0, "pan": 0, "width": 1, "mute": false,
                 "sends": [ { "db": -100, "pre": false }, { "db": -100, "pre": false } ],
                 "inserts": [ { "id": "<plugin id>", "name": "Glue", "state": "...", "bypassed": false, "mix": 1 } ],
                 "spatial": { "azimuth": 0, "elevation": 0, "spread": 60, "distance": 0.7 } },
  "spatial": { "on": true, "yaw": 0 },
  "patchbay": {
    "links": [ { "from": "track2", "to": "fx1", "gain": -6 } ],
    "synths": [ { "id": "synth", "name": "Synth", "state": "..." }, null, null, null ],
    "effects": [ { "id": "<plugin id>", "name": "Reverb", "state": "..." }, null, null, null ]
  },
  "view": { "mixer": true }
}
```

- Times are in seconds, so a project opens the same at any sample rate.
- File paths are relative to the project, so a folder of project and files can move; files
  more than one folder up are kept absolute. Sounds taken from the Sample menu are saved as WAVs
  in `<project> Samples/`, bounces in `<project> Bounces/`, clip edits in `<project> Edits/`.
- Patch points are named (`editor`, `sampler`, `slicer`, `synth1`..`4`, `track1`..`8`,
  `mixbus`, `fx1`..`4`, `main`), as in `settings.ini`, so the numbering inside the patchbay can
  change. A synth or effect already in its slot with the same id keeps running and takes the
  saved settings. The grid's rows and columns are a matter of layout and stay in `settings.ini`.
- `preMaster` is the bus the tracks with `toPreMaster` go to instead of the master (up to four
  inserts); a project without it has every track going to the master.
- `spatial` at the root is the project's Spatial mode and how far the scene is turned; each
  track, `preMaster` and each of `returns` keeps its 3D panner under `spatial`
  ([spatial-mixing.md](spatial-mixing.md)). The headphones, HRTF set, speakers and first output
  are the studio's, so they stay in `settings.ini` under `mixer/spatial`. A project without
  `spatial` is stereo.
- New Project clears the patchbay's synths and effects and puts every patch back to default.

## In the code

- `MixerWindow::writeMix` and `loadMix` (`Source/ui/MixerWindow.cpp`) write and read the mix and
  hand the rest to `Hooks::projectState` and `Hooks::setProjectState`; `Hooks::resetProject` runs
  for a new project.
- `Source/ui/ProjectSection.cpp`: those hooks on the main window, `openProject`,
  `restoreProject` (at start, only after a crash: `last.phi` offered back; the patchbay from
  `settings.ini` when the project has none) and `mixerInView()`, the view hook.
- `MainWindow::patchBayJson`, `setPatchBayJson` and `resetPatchBay` in `PatchBaySection.cpp`.

## Not yet

- The editor's own tabs (files open, their places and zoom) are not part of a project; the
  editor's crash recovery still brings back unsaved edits.
- The Sample menu's Sampler and Slicer, the Tracker and the Device Chain keep their own state.
