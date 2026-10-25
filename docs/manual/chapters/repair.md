# Repair and Restoration

PHI has two kinds of repair. **Spectral repair** works on a shape you draw in the spectral view: a cough, a squeak, a phone ringing in the middle of a take. **Restoration** works on a whole selection: noise, clicks, clipping, hum and sibilance. The **Diagnostics** panel finds problems for you. All of it is in the Repair menu and the command palette, shows a progress bar with Cancel, and is undoable.

## Spectral repair {#spectral-repair}

[]{.ix t="Spectral repair"}Switch to the spectral view (`Shift+D`) and select the sound to repair (see [The time-and-frequency selection](#spectral-selection)). Then:

| Command | Keys | Does |
|---|---|---|
| Auto Heal | `Ctrl+U` | Rebuilds the selection from the sound around it |
| Spectral Gain | `Ctrl+Alt+G` | Turns the selection down or up, with soft edges |
| Delete | `Del` | Removes just those frequencies, in just that time |

[]{.ix t="Auto Heal"}**Auto Heal** is for clicks, coughs, squeaks and phone rings, up to 4 seconds at a time. Steady tones carry on through the gap, and background noise is filled in at its own level, so the repair does not leave a hole.

![Before: a click across the spectrum.](images/auto-heal-before.png)

![After Auto Heal: the click is gone and the sound around it carries on.](images/auto-heal-after.png)

Audio outside the selection is never touched.

### Drawing tools

[]{.ix t="Lasso tool"}[]{.ix t="Brush tool"}[]{.ix t="Spot Healing Brush"}A box does not fit every sound. Four tools draw other shapes:

| Tool | Key | Use |
|---|---|---|
| Box | `V` | The usual drag |
| Lasso | `O` | Draw around a sound |
| Brush | `B` | Paint over it |
| Spot Healing Brush | `H` | Paint over a sound, and it is healed when you let go |

`[` and `]` make the brush smaller and larger. Hold `Shift` to add to the shape. Auto Heal, Spectral Gain and Delete all work on the drawn shape.

[]{.ix t="Add Harmonics"}**Add Harmonics** (`Ctrl+Alt+H`) extends a selection to the sound's overtones, for a whistle or a hum with partials: select the lowest line, add its harmonics, then heal them all at once.

![A lasso drawn around the lead melody, extended up through its harmonics.](images/lasso-harmonics.png)

### Find Similar Sounds

[]{.ix t="Find Similar Sounds"}Select one click, breath or chirp, and Repair > Find Similar Sounds (`Ctrl+Shift+F`) finds the others like it in the file. Then heal them all in one undoable step, or mark them as regions to check first.

## Restoration {#restoration}

[]{.ix t="Restoration"}These work on the selection, or the whole file.

### Noise Reduction

[]{.ix t="Noise Reduction"}[]{.ix t="Noise profile"}

::: steps
1. Select a stretch of noise on its own: the room before someone speaks, the hiss between songs.
2. Repair > Learn Noise Profile (`Ctrl+Alt+N`).
3. Select what you want to clean, or nothing for the whole file.
4. Repair > Noise Reduction (`Ctrl+Alt+Shift+N`), and set how much to take away.
:::

### DeClick, DeClip, DeHum and DeEss

[]{.ix t="DeClick"}[]{.ix t="DeClip"}[]{.ix t="DeHum"}[]{.ix t="DeEss"}

| Command | Removes |
|---|---|
| DeClick | Clicks and vinyl crackle, rebuilt by least-squares interpolation |
| DeClip | Flattened peaks of a recording that clipped, rebuilt to their likely shape |
| DeHum | Mains hum at 50 or 60 Hz, found automatically, with its overtones |
| DeEss | Harsh "s" and "sh" sounds |

## Diagnostics {#diagnostics}

[]{.ix t="Diagnostics panel"}The Diagnostics panel (`Ctrl+Shift+D`) is modelled on Audition's. Choose what to look for, then **Scan** the selection or the whole file:

- **Clicks and crackle**
- **Clipping**
- **Dropouts**: lost samples, where the sound cuts to silence for an instant
- **Silence**

![The Diagnostics panel after a scan for clicks.](images/diagnostics.png)

Move through the list to see and hear each problem in the waveform. Then **Repair** the ones you chose, **Repair All** in one undoable step, **Mark** them as regions, or, for silence, **Delete** them.
