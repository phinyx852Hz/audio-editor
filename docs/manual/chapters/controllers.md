# Controllers and MIDI Mapping {#controllers}

Any MIDI keyboard or controller plays PHI the moment it is plugged in. Three tools go further: **Control Surfaces** drives a Launchpad, Launchkey or Push as Ableton and Bitwig do, **MIDI Learn** maps a controller's knobs and buttons from a list, and **MIDI Map Mode** maps them by pointing at the mixer.

All three are in the Performance menu.

## MIDI inputs

[]{.ix t="MIDI inputs"}Sample > MIDI Inputs lists the MIDI inputs. Each plays from the moment it is plugged in; PHI looks for new ones every two seconds. Untick an input to keep it out. Inputs are remembered by name, so a device keeps its setting however it is plugged in. In the mixer, the **MIDI...** button does the same, and the keyboards play the armed track.

## Control surfaces {#control-surfaces}

[]{.ix t="Control surfaces"}[]{.ix t="Push"}[]{.ix t="Launchpad"}[]{.ix t="Launchkey"}Performance > Control Surfaces lists the surfaces plugged in: Novation Launchpads (X, Mini MK3, Pro MK3, Pro, MK2) and Launchkeys (MK3, Mini MK3, MK4), and Ableton Push 2 and 3. PHI finds them by name and drives them at once, with no drivers; untick one to use it as a plain MIDI controller. The device knowledge comes from DrivenByMoss.

On the pads there are three views, as in Live:

- **Session**: the clip launcher, tracks across and scenes down, lit in the tracks' colours (green and pulsing while playing, red while recording). Press an empty slot on the armed track to record into it. The buttons beside the pads launch scenes.
- **Note**: the pads play the armed track's instrument, a fourth between rows, in key or chromatic.
- **Mixer** (8 × 8 pads): each column is a track's fader, stop, mute, solo and arm.

::: hardware
Push's display is driven over USB, which needs PHI's udev rule (`70-phi-push.rules`, installed with PHI). Without it everything but the display works; Control Surfaces says why the display is off.
:::

## MIDI Learn {#midi-learn}

[]{.ix t="MIDI Learn"}Performance > MIDI Learn maps any knob, fader, key, pad or button of any controller, such as a keytar or an Intech Grid. Choose what it should do (play, record, the metronome, a track's volume, pan, sends, arm, mute or solo, a slot or scene to launch, the master, the tempo…), press **Learn** and move the control. For endless encoders, set the mapping's **Mode** to the relative kind they send. Mappings are kept in PHI's settings.

## MIDI Map Mode {#midi-map-mode}

[]{.ix t="MIDI Map Mode"}MIDI Map Mode is the quickest way to map a controller, as in Ableton Live: point at a control, then move the hardware.

::: steps
1. Press `Ctrl+M` (Performance > MIDI Map Mode). The mixer comes to the front with every control you can map tinted, and the **MIDI Mappings** window opens beside it with the full list.
2. Click a tinted control. It is outlined.
3. Move a knob or fader, or press a key, pad or button, on any keyboard or controller. The control is mapped, and anything mapped to that hardware control before gives way.
4. Map as many as you like, then press `Esc` or `Ctrl+M` again to leave.
:::

What can be mapped: each track's fader, pan, Send A and Send B, mute, solo and arm; the master fader; the tempo; Play, Loop, Record and Click; and, while the clip launcher shows, every slot, each track's stop button, each scene and Stop All.

Mapped controls are tinted green and show what drives them (`CC 21`, `N 36`, `PB`). `Delete` removes the outlined control's mappings; in the MIDI Mappings window, `Delete` removes the chosen row. The mouse wheel still scrolls under the tint. Instrument and plugin parameters cannot be mapped this way yet; use a Prism's macros (see [Prisms](#prisms)).
