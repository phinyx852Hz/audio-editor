# DAWproject: projects from Bitwig, Studio One, Cubase and others

[DAWproject](https://github.com/bitwig/dawproject) is the open project format that Bitwig and
PreSonus made so that projects can move between programs. Bitwig Studio (5.0.9 on), Studio One
(6.5 on), Cubase (14 on) and others read and write it. PHI opens one as a PHI project and writes
its own projects as one.

- **Open:** Mixer > Import DAWproject or Live Set..., or open the `.dawproject` file like a project (Mixer >
  Open Project, File > Open, the command line, the file manager).
- **Write:** Mixer > Export DAWproject...

Bitwig's own `.bwproject` files are closed and undocumented; export the project from Bitwig as a
DAWproject (File > Export > DAWproject) and open that. Ableton Live does not write DAWproject; its
`.als` sets open the same way, a Drum Rack on PHI's Slicer: [ableton-sets.md](ableton-sets.md).

## Opening one

The project is unpacked into a folder beside the file, `<name> PHI/`, which holds the PHI project
`<name>.phi`, the audio in `Audio/` and each note clip as a MIDI file in `MIDI/`. That `.phi` is
then open, so saving keeps it there and the `.dawproject` is left as it was. When it is done, a
message lists what did not carry over (Show Details).

| DAWproject | In PHI |
|---|---|
| Tracks (16 at most) | Mixer tracks, with their names and colours. A track with only notes is an instrument track, with only audio an audio track, with both a hybrid. |
| Volume, pan, mute, solo | The strip's fader, pan, mute and solo. |
| Effect tracks (FX, returns) | Returns A, B, C... (8 at most), named after them, with their fader, pan and mute. One plugin effect goes in the return's slot; several go in series into a Prism of effects there (up to 8, in its one ray). |
| Sends to effect tracks | Sends A, B, C..., before or after the fader as they were. |
| Master volume | The master fader. |
| Audio clips on the timeline | Audio clips, with their start, the part of the file heard, and mute. A looped clip is laid out as one clip per pass. |
| Note clips on the timeline | MIDI clips (a MIDI file each, at the project's tempo). Loops are unrolled, notes are cut at the loop's end, and clips inside clips are flattened. Velocity, release velocity and channel are kept. |
| Scenes and their clip slots | The clip launcher's scenes (with their names) and slots, looping. |
| VST3 and CLAP plugins | The same plugin by name and format among PHI's plugins, with its settings: a VST3 from its `.vstpreset`, a CLAP from its state. The instrument goes in the instrument slot, the effects in the inserts (8 at most), bypassed if they were off. A plugin PHI has only in another format (an LV2 of the same name) loads without its settings. |
| Tempo, time signature | The tempo box and beats to the bar. |
| `metadata.xml` | Read; the title is shown nowhere yet. |

Not carried over, and said so: groups (their tracks are kept, the group's own volume and devices
are not), VCAs, the other program's own devices (EQ+, Compressor, Simpler...), automation, tempo
and time signature changes (PHI keeps the first tempo), warping (a stretched clip plays at its own
speed), fades, markers, per-note expressions, video, and the master's devices.

## Writing one

Everything goes into the one file, so it can be opened on another computer: `project.xml` and
`metadata.xml` (the project's name as its title), each audio file once under `audio/`, and each
plugin's settings under `plugins/`. Times are written in beats at the project's tempo.

| PHI | DAWproject |
|---|---|
| Tracks | Tracks, with name, colour, fader, pan, mute, solo and sends. |
| Audio and MIDI clips | Audio clips (the part of the file heard) and note clips (the notes heard, the strip's transpose included, except on the drums' channel 10). |
| Launcher slots | Scenes with clip slots. |
| VST3 and CLAP instruments and inserts | Plugins with their settings in their own format, so the other program loads them as they were. |
| Returns in use (A, B...) | Effect tracks, with their names and plugin effect (a Prism's first ray, in series). |
| Tempo, beats to the bar, master | Tempo, time signature (over 4), master volume. |

Not written, and said so: PHI's own instruments (Synth, Sampler, Slicer, Raga, Prism; bounce the
track first to keep its sound), LV2 plugins and PHI's built-in effects (they have no settings
another program could load), clips that have no file yet, reversed clips (written forwards), the
strip's trim, width, cuts and delay, the Pre-Master bus, and the patchbay.

## In the code

- `Source/mixer/DawProject.h`, `.cpp`: the format, plain C++. `read` unpacks a `.dawproject` into a
  `dawproject::Project`, `write` writes one, `songOf` and `notesOf` turn notes into a MIDI file and
  back. Tested in `tests/CoreTests.cpp` (`testDawProject`), and what `write` produces validates
  against the format's `Project.xsd`.
- `Source/core/Zip.h`, `.cpp`: reading and writing zip archives on zlib (no Zip64, so 4 GB at most).
- `Source/engine/PluginNative.cpp`: a plugin's state in its own format, through JUCE's VST3 preset
  calls and PHI's CLAP host; `nativeState` and `setNativeState` on instruments and effects.
- `Source/ui/DawProjectSection.cpp`: `MixerWindow::importDawProject` (builds the `.phi` and opens
  it, then gives the plugins their states) and `exportDawProject`.

Not yet tried against files from Bitwig, Studio One or Cubase themselves; the test files follow
the format's schema and the reference implementation's examples. Bitwig's `.clap-preset` files
are taken to be the plugin's state stream as it is, which has not been checked.
