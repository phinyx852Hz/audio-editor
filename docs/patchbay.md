# PHINYX Patchbay

Status: step 1 built (2026-10-22). Owner's ask (2026-10-22): "a DAW patchbay panel that would
pop out, sort of like Bitwig's grid, but a big 8X8 patch bay that would be dynamically populated
by what's active in the DAW or what could be designed by various components in the DAW as well
as standalone synths and samplers." The owner's notes on Bitwig's Grid, device chains,
modulation and a project-wide patchbay (Daw Patchbay.docx) set the direction below.

## What it is

One routing graph for everything PHINYX plays live, and an 8x8 grid to patch it. The patchbay
is the router itself, not a second engine on top: the mixer's tracks, the Sampler, the Slicer and
the editor's playback all go through it, and the Mixer and the grid show the same routing. This
is the notes' central rule: "one authoritative routing graph shared by the patchbay, track mixer,
device chains, and audio engine".

Every point is stereo:

| Point | Output (row) | Input (column) |
| --- | --- | --- |
| Editor: the open file, after the Master Section | yes | |
| Sampler and Slicer (Sample menu) | yes | |
| Synth 1 to 4: the patchbay's own instruments (built-in Synth or any instrument plugin) | yes | |
| Track 1 to 8 of the Mixer: output after its fader; input before trim, pan and fader | yes | yes |
| Mix Bus: the mixer's master fader | yes | yes |
| FX Bus 1 to 4: one effect each (any effect plugin, built-in or stock effect), or none | yes | yes |
| Main Out: the speakers | | yes |

Out of the box every source goes where it went before the patchbay: the editor, Sampler,
Slicer, synths, FX buses and Mix Bus to the Main Out, and each track to the Mix Bus. So nothing
changes until you patch something.

## The window

Mixer > Patchbay (`F4`) pops it out. Sources run down the side, destinations across the top.
- **Auto-fill** (on by default) fills the 8 rows and 8 columns with what is in use (a file in the
  editor, the Sampler and Slicer with a sound, the mixer's tracks while it is open, synths with
  an instrument, FX buses with an effect or anything patched in) and keeps up as that changes.
  The Main Out always has the last column. Everything else waits off the grid with its patches.
- Click a **row or column name** for its menu: put something else there (two slots swap), pick a
  synth's instrument or an FX bus's effect and open its editor, open the Sampler, Slicer or
  Mixer, play a synth from the MIDI keyboard, or unpatch everything it has. Choosing a slot by
  hand turns Auto-fill off; Fill from What Is in Use turns it on again.
- Click a **jack** to patch or unpatch. A lit jack takes the source's colour and glows with what
  goes through it. Drag it up or down, scroll, or press `+` and `-` for its gain (`Shift`, or
  `[` and `]`, for 0.1 dB), from off (−60 dB and below) to +12 dB; double-click or `0` for 0 dB.
  Right-click for set gains. The arrow keys move between jacks, `Space` patches.
- **Crossed-out jacks** would feed a signal back into itself (FX Bus 1 already reaches Track 2,
  so Track 2 can't go into FX Bus 1); the status line says which way round. A dash is a pair
  with no ports (a source into itself).
- Each name has a level meter. Default Patches puts every source back where it started.
- Patches, synths, effects (with their settings) and the grid's layout are kept in
  `settings.ini` and come back next time; if anything is patched, it is heard straight away.

## How it plays

`Source/patchbay/PatchBay.*` (plain C++, tested in CoreTests): the graph is checked on every new
patch (a path back from the destination to the source refuses it), sorted so sources play before
what they feed, and handed to the audio thread as a plan. Each block: the editor's sound comes
in, the mixer plays its tracks in the plan's order (so Track 2 into an FX bus into Track 1 has no
block of delay), taking what is patched into each track and handing each track's sound back
instead of summing it; then every other point plays once, summing its inputs with ramped gains;
the Main Out goes to the speakers. Gains are atomics, so dragging a jack never takes a lock;
patching swaps the plan.

## Not yet

- Mix Down to Editor renders the mixer's own sum, as if every track went to the Mix Bus.
- No latency compensation: an FX bus with a lookahead plugin runs late against a dry path.
- Stereo only. The notes' format negotiation (7.2.4, Ambisonics, binaural, with adapters the
  owner approves) needs typed multichannel ports first.
- No audio inputs (live inputs come with the mixer's step 5) and no MIDI or modulation patches:
  the notes keep those as separate kinds of connection, which fits a second grid page.
- Pages beyond 8x8, pre/post-fader taps, inserts inside a track, clip-level sources.
