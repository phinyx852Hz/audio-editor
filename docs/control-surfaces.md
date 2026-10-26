# Control surfaces and MIDI Learn

Status: built 2026-10-26, not yet tried on hardware. The owner has every device below except a
Push 3 and will test them.

## What the owner asked for (2026-10-26)

"One thing next might be the MIDI controller integration that Ableton and Bitwig have. I want
direct support for Novation Launchkey series of keyboards, Novation Launchpad series, Ableton Live
Push (I have the 2, but 3 would be great), Alesis Vortex Wireless 2 keytar, the Roland AX-edge
Keytar, and any script offered by Intech.studio." On DrivenByMoss: "Yes, please use that for PHI!!"

## Where the knowledge comes from

No drivers are needed: every device here is class-compliant USB MIDI, and Linux (ALSA) sees its
ports as soon as it is plugged in. What a DAW needs is each device's protocol: which port to talk
to, the message that puts it in a mode a DAW drives, which number each pad and button sends, and
how to light them. Novation publishes Programmer's Reference Guides for the Launchpads and
Launchkeys, and Ableton publishes the Push 2 MIDI and Display Interface Manual on GitHub. PHI's
device code ports these facts from [DrivenByMoss](https://github.com/git-moss/DrivenByMoss) by
Jürgen Moßgraber, the open-source extensions that drive these devices (and many more) in Bitwig
and Reaper. DrivenByMoss is LGPL-3.0, which PHI's AGPL-3.0 can carry, and each ported file credits
it (`third_party/README.md`). It is Java for Bitwig's and Reaper's APIs, so PHI's code is its own
C++; only the device knowledge is shared. No vendor files, firmware or Ableton control scripts are
used.

## How it works

Performance > Control Surfaces lists what is plugged in. A surface is found by its MIDI ports'
names, and PHI drives it at once through the port that device uses for a DAW (the Launchpads'
MIDI port, the Launchkeys' DAW port, Push's Live port). Every two seconds PHI looks again, so a
device plugged in or taken out is picked up or let go by itself. Untick one to use it as a plain
MIDI controller instead. The port a surface uses is kept out of the MIDI Inputs list; its other
ports stay there, so a Launchkey's keys still play the armed track as a keyboard.

On a surface's pads there are three views, as in Live:

- **Session**: the clip launcher. Columns are tracks, rows are scenes. A clip is lit in its
  track's colour, green and pulsing while it plays, flashing while it waits for the quantise, red
  while it records. An empty slot on the armed track is dim red: press it to record into it, as
  the slot's record dot does (docs/recording.md). Pressing a recording slot steps its take on, as
  Record does. An empty slot on an unarmed track stops the track. The buttons beside the pads
  launch scenes (lit dim when the scene has clips, green when one plays). Up and Down move the grid
  through the 32 scenes (Shift: a page at a time). Left and Right select the track. Shift with a
  pad selects its track.
- **Note**: the pads play the armed track's instrument (or whichever instrument the MIDI keyboard
  is playing), a fourth between rows: in key (C major, the root lit in the track's colour) or
  chromatic. C3 is at the bottom left; Up and Down (or Push's Octave buttons) change the octave.
  Push's touch strip bends the pitch here.
- **Mixer** (8x8 pads only): each column is a track. The top four rows set its fader (0, -6, -15
  and -30 dB, lit up to where it is), then stop its clip, mute, solo and arm.

Held Stop Clip, Mute, Solo or Arm buttons (Push, Launchpad Pro) make a pad or track button do that
to its track; tapped alone they do it to the selected track (Shift with Stop: every track).

### Launchpad (X, Mini MK3, Pro MK3, Pro, MK2)

Driven in programmer mode; the pads are lit by RGB (the MK3 generation) or the Novation palette.
On the X and Mini MK3 the top row is Up, Down, Left, Right, Session, Note (Drums), Mixer (Custom or
Keys) and Shift (Capture MIDI or User: tapped alone it captures, held it is Shift). The Pros add
Play, Record (Shift: Capture), Click, Undo, the track buttons along the bottom and Arm, Mute,
Solo, Stop Clip and Volume (the Mixer view).

### Launchkey (MK3, Mini MK3; MK4 as an MK3)

Driven in DAW mode on its DAW port. Its two rows of pads are two scenes of the launcher (the
round buttons beside them launch them), its knobs follow the mode chosen on the keyboard (volume,
pan, send A or B), its faders and the master fader set the faders, and the fader buttons select
tracks or (after the Arm/Select button) arm them. Play (Shift: Stop), Stop, Record (Shift:
Capture), Loop, Capture MIDI, Click and Undo (Shift: Redo) work the transport. In Drum mode the
pads play the armed track. The MK4 has not been checked against its own reference.

### Push 2 and 3

Driven on the Live port; PHI writes its palette into Push's colour table and puts a Push 2 into
Live mode. Session (and the Session button) shows the launcher, Note plays, Layout toggles the
Mixer view. The 8 buttons under the display select tracks, the 8 over it arm them. The 8 encoders
turn volume, pan, send A or send B (Mix steps through them; Shift turns them finely), the master
encoder the master fader, the tempo encoder the tempo (Shift: tenths). Play (Shift: Stop), Record
(Shift: Capture), Metronome, Tap Tempo, Undo (Shift: Redo), the arrows, Octave, Scale (in key or
chromatic), Mute, Solo and Stop Clip work as above. Push 3 uses the same messages; it has not been
tried. **The Push display** stays dark for now: it is driven over USB rather than MIDI (libusb,
960x160 pixels, documented by Ableton) and comes next.

## MIDI Learn

Performance > MIDI Learn maps any knob, fader, key, pad or button of any keyboard or controller:
choose what it should do (play, record, capture, metronome, a track's volume, pan, sends, arm,
mute or solo, a slot or scene to launch, the master, the tempo...), press Learn and move the
control. From then on it does that, from that device and channel, and no longer plays notes.
Endless encoders: set the mapping's Mode to the relative kind they send (two's complement,
offset 64, or sign and magnitude). Mappings are kept in PHI's settings.

- **Alesis Vortex Wireless 2 and Roland AX-Edge**: they play the armed track as keyboards (MIDI
  Inputs), with their pitch bend, modulation and other controllers going to the instrument.
  Their buttons, pads, knobs and strips can be learned to the mixer and the transport.
- **Intech Studio Grid**: its modules send MIDI as their profile says. Learn their encoders,
  faders and buttons (encoders set to relative in Grid Editor need a relative Mode here). A PHI
  profile for Grid Editor's Profile Cloud would map a whole module at once; that is for later.

## Code

- `Source/control/ControlSurface.h/.cpp`: the State surfaces show, the Commands they send, the
  palette, the lights sent only when they change, and the Session, Note and Mixer views (plain C++,
  tested in CoreTests).
- `Source/control/Devices.cpp`: the Launchpads, Launchkeys and Push, and how their ports are found.
- `Source/control/MidiLearn.h/.cpp`: the mappings.
- `Source/engine/ControlSurfaces.h/.cpp`: the ports (JUCE): found, opened, read on the MIDI thread
  and written from the UI.
- `Source/ui/ControlSection.cpp`: the menu commands, the dialogs and the timer (30 a second: the
  surfaces' commands, then their lights). `Source/ui/MixerControl.cpp`: the mixer as they see it
  and what their commands do to it.
