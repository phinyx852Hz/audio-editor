# Ableton Live Sets

PHI opens Ableton Live Sets (`.als`, Live 9 to 12) as PHI projects, the same way it opens a
[DAWproject](dawproject.md): Mixer > Import DAWproject or Live Set..., or open the `.als` like a
project (Mixer > Open Project, File > Open, the command line, the file manager). Live itself
does not write DAWproject, and Ableton does not document its format; PHI reads the gzipped XML
Live saves.

A folder beside the set, `<name> PHI/`, gets the PHI project `<name>.phi` and each MIDI clip as
a MIDI file in `MIDI/`; the samples stay where they are and the project points at them. Then
the `.phi` is open. When it is done, a message lists what did not carry over (Show Details).

| Live | In PHI |
|---|---|
| MIDI and audio tracks (16 at most) | Mixer tracks with their names: instrument tracks and audio tracks. |
| Volume, pan, track on/off, solo | The strip's fader, pan, mute and solo. |
| Return tracks | Returns A, B, C... (8 at most), named after them, with their fader, pan and mute; their VST3 effects if PHI has them, several in series in a Prism of effects. |
| Sends | Sends A, B, C... |
| Master volume, tempo | The master fader and the tempo box; the first clip's time signature. |
| Arrangement MIDI clips | MIDI clips, loops unrolled from the clip's start marker, disabled notes left out, velocity and release velocity kept. |
| Arrangement audio clips | Audio clips: warped ones through their warp markers to the part of the file heard, unwarped ones by their start in seconds; a looped clip is one clip per pass. |
| Session view clips | The clip launcher's slots, in their scenes (named as in Live). |
| **Drum Rack** | **PHI's Slicer** on the track: a pad for each of the rack's pads, in order from bank A (so Push plays them), each answering the note it did in Live, so the clips play the same sounds. Each pad gets its Simpler's sample, sample start and end, level (the Simpler's volume, the sample's and the pad chain's), pan, transpose and fine tune, One-Shot or Classic, its choke group and whether its chain was off. |
| Simpler or Sampler alone | PHI's Sampler with that sample, at its own settings. |
| VST3 plugins | The same plugin by name among PHI's plugins, with its settings when Live kept them (untested). |

Samples are looked for where Live 11 and 12 say they are, then beside the set where Live 10
and before keep them, then where they were on the computer that saved the set, and last by name
anywhere in the set's folder, so a set copied from Windows or macOS finds its samples once they
are in its folder (Live's File > Collect All and Save puts them there). The samples that are not
found are listed.

Not carried over, and said so: Live's own instruments and effects (Operator, Wavetable, EQ Eight,
Reverb...), MIDI effects, racks other than Drum Racks (Instrument and Audio Effect Racks), pads
that are not a Simpler or Sampler, a Sampler's samples after its first, VST2 and Audio Unit
settings, groups (their tracks are kept), automation, tempo changes, warping that stretches
(a stretched clip plays at its own speed), clip envelopes, locators, and the master's devices.
Track colours are not read.

## In the code

- `Source/mixer/AbletonSet.h`, `.cpp`: the set read into a `dawproject::Project`, with the Drum
  Rack in `Track::kit` and a lone Simpler in `Track::samplerFile`. A pad's note is 128 less
  Live's `ReceivingNote`. Tested in `tests/CoreTests.cpp` (`testAbletonSet`), and tried on a
  Live 10 set with a 32-pad Drum Rack.
- `Source/ui/DawProjectSection.cpp`: the same import as a DAWproject's; the kit becomes the
  track's Slicer kit in the `.phi`.
