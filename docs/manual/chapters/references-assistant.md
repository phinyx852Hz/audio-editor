# References and the Master Assistant

## Reference tracks {#references}

[]{.ix t="References"}[]{.ix t="A/B comparison"}A mastering engineer keeps a finished song or two at hand, to check a master against something known to sound right. PHI's References panel (`Ctrl+Alt+Shift+R`, also in the Master menu) does this, like WaveLab's reference tracks.

![The References panel with two reference songs.](images/references.png)

::: steps
1. In the panel, press `Insert` to add finished songs, or earlier versions of your own.
2. Play your file, and press `A` to switch to the chosen reference at the same moment in the song. Press `A` again to come back.
3. `Shift+A` goes to the next reference.
:::

The switch crossfades briefly, so it never clicks. While a reference plays, "B" and its name show at the top right of the waveform, and the meters measure what you hear.

[]{.ix t="References!loudness matching"}**Loudness matching.** A louder version always sounds better at first, so PHI matches each reference's loudness to your audio as it sounds through the Master Section, measured on the first switch and again with **Match Loudness**. A **Level** offset per reference (`+` and `-` in the list) adjusts the match.

By default references play without the Master Section, kept in time with it; untick the box to hear them through it. The list is remembered.

## The Master Assistant {#master-assistant}

[]{.ix t="Master Assistant"}Master > Master Assistant (`Alt+Shift+M`) is like the mastering assistants of Ozone, with one difference: it only turns the knobs of the chain you built, or loaded from a preset. It never adds, removes or moves a plugin.

![The Master Assistant: the target, what to match, and every knob it may turn.](images/master-assistant.png)

### Choosing a target

Aim for one of:

- **a reference** from the References panel: its loudness, peak-to-loudness ratio, tonal balance in octaves and stereo width, compared as Matchering compares them;
- **a preset**: Streaming −14 LUFS, Apple Music and podcasts −16, Amazon −14 at −2 dBTP, Loud −9, EBU R128 −23, with a tone of your own (Balanced, Warm or Bright) and a true-peak ceiling.

Tick **Loudness**, **Tone**, **Dynamics** and **Width** to say what to match, and choose how far a knob may move from where you left it.

### Which knobs it turns

The list shows every parameter of every plugin in the Master Section, inside Prisms too, from any maker. Gains, EQ band gains, thresholds, ratios and widths are ticked to start with; frequencies, times, ceilings, modes and switches are not. A knob only moves for its own job: an EQ band when the tone is matched, a width when the width is, so nothing is widened just to get louder.

### Suggest, listen, keep

**Suggest** renders about 30 seconds of the loudest parts and one typical part through the chain for every try, measures it, and turns the ticked knobs a step closer each time (a Levenberg-Marquardt fit). It usually takes 10 to 60 seconds; playback stops meanwhile. Then it shows each knob's old and new value, and the measures before, after and in the target, for the whole song.

You hear the suggestion at once:

| Control | Does |
|---|---|
| Amount | Blends between your settings and the suggestion |
| `A` | Switches between them |
| `Space` | Plays |
| Keep | Keeps what you hear |
| Revert / `Esc` | Puts every knob back |

::: tip
The Assistant can only reach a target the chain can reach. If the loudness will not come up, the chain needs a limiter; if the tone will not match, it needs an EQ. Add them, then Suggest again.
:::
