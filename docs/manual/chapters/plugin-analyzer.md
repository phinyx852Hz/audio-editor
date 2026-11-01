# Analysing a Plugin {#plugin-analyzer}

[]{.ix t="Plugin Analyzer"}The **Plugin Analyzer** measures what a plugin does to sound: its frequency and phase response, its distortion and aliasing, how it compresses, how it moves, and whether its maker used the golden ratio. It works like the measuring tools of a test bench (Plugin Doctor is the best-known one) and is built into PHI.

::: important
**You don't load a sound, and you don't press play.** The Analyzer makes its own test signals: a sweep, steady tones and level steps. It sends them through a **copy** of the plugin, with the knobs as they are now, and reads what comes out. Nothing you hear changes, the plugin in the Master Section is left alone, and the open file doesn't matter. Every plugin gets exactly the same signals, so measurements made on different days, or of different plugins, can be compared.
:::

![The Plugin Analyzer measuring Φ EQ: the frequency response above, the phase below.](images/plugin-analyzer.png)

## Opening it

[]{.ix t="Plugin Analyzer!opening"}Any of these opens the Analyzer:

- Analyze > Plugin Analyzer (`Ctrl+Alt+U`);
- **Analyze** in the Master Section panel (`Ctrl+Shift+E`), or `A` on a slot in its list, which measures that slot at once.

The Analyzer measures a plugin that is in the Master Section. If the plugin isn't there yet, you can add it in either of two ways:

- **Load a Plugin...** in the Analyzer puts the plugin into the first free slot and measures it. It is **bypassed**, so you don't hear it while you work.
- Or add it as usual, with Master > Add Plugin (`Ctrl+Alt+E`); see [Adding a plugin](#master-section).

A bypassed slot is still measured: the copy is measured as if it were on.

## The controls

[]{.ix t="Plugin Analyzer!controls"}The top row chooses what is measured. The second row chooses how.

| Control | Does |
|---|---|
| **Measure** (the list) | Chooses what to measure: a slot, shown as *Slot 3: Φ EQ*, or **The whole Master Section**, every slot in order |
| **Load a Plugin...** | Puts a plugin into a free slot, bypassed, and measures it |
| **Plugin Window** | Opens the plugin's window, so you can turn its knobs while it is measured |
| **Level** | How loud the test signals are, as a peak in dBFS, from −60 to +6. The default is **−12 dBFS** |
| **Both channels / Left only / Right only** | Which channels the test signals go into. Use one channel alone to see how much leaks into the other (crosstalk) |
| **Frequency, Harmonics, Aliasing, Dynamics, Modulation** | Which tests to run. The first three are ticked to start with |
| **Live** | On by default: measures again by itself every time a setting of the plugin changes |
| **Measure** (the button), `F5` | Measures now |
| **Copy Report** | Copies every number measured, as text, to the clipboard |

The status line at the bottom names the plugin and its latency, **reported** (what the plugin tells PHI) and **measured** (where its impulse really peaks). When the two differ, the plugin reports its delay wrongly, and PHI's delay compensation will be off by the difference.

### Which level?

[]{.ix t="Plugin Analyzer!test level"}For a plugin that doesn't care about level, such as an EQ, a delay, a reverb or a chorus, the level hardly matters. Leave it at −12 dBFS.

For a plugin that does care about level, such as a compressor, limiter, saturator or tape model, the level is part of the test. At −12 dBFS a compressor with its threshold at −18 dB is already compressing the sweep, so the frequency response shows its gain reduction as well as its tone. Measure it at a level below its threshold to see the tone alone, and at a level above it to see the compression. Write down the level next to every result.

The Dynamics test doesn't use this level: it plays its own levels, from −60 to 0 dBFS (see below).

### Which tests?

The fewer tests are ticked, the faster each measurement is, and the more smoothly Live follows the knobs. Tick what the plugin is for:

| Plugin | Tick | Look at |
|---|---|---|
| EQ, filter, tone control | Frequency | Frequency, Impulse |
| Delay, echo, reverb | Frequency | Impulse (All of it), Frequency |
| Saturation, tape, exciter, amp | Frequency, Harmonics, Aliasing | Harmonics, Aliasing |
| Compressor, limiter, gate, expander | Frequency, Dynamics | Dynamics, then Harmonics |
| Chorus, phaser, flanger, tremolo, auto-pan, rhythmic gate | Frequency, Modulation | Modulation, Frequency |
| Pitch shifter, harmonizer | Harmonics | Harmonics (the tones it adds) |
| Anything, to look for φ | all of them | Forensic Φ |

Modulation is the slowest test, because it listens to 12 seconds of a steady tone.

## The tests and their tabs

### Frequency

[]{.ix t="Plugin Analyzer!frequency response"}[]{.ix t="Frequency response"}The test signal is a 2-second exponential sweep from 10 Hz up to just below the Nyquist frequency, followed by 1.5 seconds of silence for the plugin to ring out. From it the Analyzer works out the plugin's impulse response, and from that everything on the Frequency and Impulse tabs.

- **Magnitude** (top) is how much each frequency is boosted or cut, in dB. A flat line at 0 dB means the plugin leaves the tone alone. Left is drawn solid, right dashed; one line means both sides are the same.
- **Phase / Group delay** (below) shows the phase shift at each frequency, or how late each frequency arrives in ms. A minimum-phase EQ turns the phase where it boosts or cuts. A linear-phase EQ keeps it flat but delays everything by the same time, its latency.

The same sweep also reads the harmonics at each frequency; they are shown on the Harmonics tab.

### Impulse

[]{.ix t="Impulse response"}[]{.ix t="Plugin Analyzer!impulse response"}The plugin's answer to a single click: a single spike at 0 ms means it changes nothing. Choose how much to show: the first 10, 50 or 200 ms, or **All of it** for echoes and reverbs. Echoes are later spikes, and a reverb is a cloud that fades away. A line marks the latency the plugin reports, so the spike should sit on it.

### Harmonics

[]{.ix t="Plugin Analyzer!harmonics"}[]{.ix t="THD"}[]{.ix t="Total harmonic distortion"}A steady tone of 1 kHz at the test level, analysed with a 65,536-point FFT. The text gives, for each side:

- **THD**, the total harmonic distortion: the harmonics' level against the tone's, in percent. A clean plugin is below 0.001 %; analogue-style saturation sits between 0.1 % and a few percent;
- **THD+N**, the same with the noise included;
- **gain**, how much louder or quieter the tone comes out;
- **H2 to H5**, the second to fifth harmonics in dB below the tone. Even harmonics (H2, H4) sound warm and full; odd ones (H3, H5) sound harder and more edgy;
- **tones that are no harmonic**: anything else in the output, such as hum, aliasing, or the tones a pitch shifter adds.

The upper picture is the spectrum of the tone after the plugin. The lower one shows H2 to H5 at every frequency of the sweep, so you can see whether a plugin distorts more in the bass than in the treble.

### Aliasing

[]{.ix t="Plugin Analyzer!aliasing"}[]{.ix t="Aliasing"}A plugin that distorts makes harmonics, and the ones above the Nyquist frequency can't exist. If the plugin doesn't oversample, they fold back down as **aliasing**: tones at frequencies that have nothing to do with the music, which sound harsh and grainy. The test plays tones from 1 kHz to 20 kHz and measures what folds back.

The upper picture shows the worst aliasing for each tone, and the lower one the spectrum of the 7.5 kHz tone, with folded harmonics marked by dashed lines. *No aliasing to speak of* means everything is below −140 dB. Below about −100 dB it can't be heard; above −60 dB it can.

### Dynamics

[]{.ix t="Plugin Analyzer!dynamics"}[]{.ix t="Transfer curve"}Two tests, at their own levels:

- **The transfer curve.** A tone from −60 to 0 dBFS in 2 dB steps, each held for 250 ms before it is read. The picture plots output level against input level, with a dashed line for unity (out = in). Where the curve bends away from the line, the plugin compresses or limits; the text reads off the **ratio** and the **threshold**. A flat top means a limiter.
- **The step.** A tone that jumps from −40 to −6 dBFS and back. The lower picture shows the gain over time. The text gives the **attack** and the **release**, each the time to get 63 % of the way, the usual definition.

A plugin whose threshold is above −6 dBFS doesn't move in the step test. Lower its threshold for that test, or judge it by the transfer curve alone.

### Modulation

[]{.ix t="Plugin Analyzer!modulation"}12 seconds of a steady tone, and how its level and pitch move. The text gives how far the level moves in dB, how far the pitch moves in cents, the **rates** of the LFOs it found, and, for a gate, the length of its steps and how many are open and closed. The picture is the level over the 12 seconds.

### Forensic Φ

[]{.ix t="Forensic Φ"}[]{.ix t="Plugin Analyzer!Forensic Φ"}This tab asks whether the plugin's maker used the golden ratio. It collects the numbers a designer chooses from every test: the frequencies of peaks and notches, echo times and levels, ratios, attack against release, LFO rates, gate rhythms, latency. Then it looks for golden ratios between them (φ^(n/2): √φ, φ, φ², ...) and for golden numbers (1.618, 2.618, 6.18, Fibonacci numbers such as 13 or 233).

Measure enough numbers and some fall near φ by luck, so every match is weighed against chance. The same search is also run for 1000 other ratios between 1.40 and 1.90, the **control scan** in the picture, with φ marked. φ only counts if it stands out from nearly all of them. The verdict is one of four:

| Verdict | Means |
|---|---|
| No φ found | No golden ratios at all |
| Consistent with chance | Some, but no more than luck would give |
| Suggestive | More than luck in one kind of evidence; worth a closer look |
| Strong evidence | φ was clearly chosen by the designer |

PHI's own Φ effects are built on φ, so they test the tab, and the tab tests them. Set one so it audibly does something (a Φ EQ with every band at 0 dB has nothing to find). If it still comes out *No φ found*, something in the effect isn't as golden as it should be.

## A routine for any plugin {#plugin-analyzer-routine}

[]{.ix t="Plugin Analyzer!routine"}To measure a plugin the same way every time, and so be able to compare it with others:

1. **Put it in the Master Section.** Use Load a Plugin... in the Analyzer, or add it to a free slot as usual. Only one plugin is needed; you can bypass it so you don't hear it.
2. **Choose it** in the Measure list.
3. **Reset it to its defaults**, or to the preset you want to measure. Setting **Mix** (Dry/Wet) to 100 % makes what the plugin does easier to see; measure at its default mix as well, as that is what you hear.
4. **Leave Level at −12 dBFS** and the input on **Both channels**, unless you are testing level or crosstalk. Note anything you changed.
5. **Tick the tests** for the kind of plugin, from the table above.
6. **Press Measure** (`F5`) and wait for the progress bar at the bottom right to finish.
7. **Read the tabs**, starting with the one the table names. Check the status line too: does the measured latency match the reported one?
8. **Turn one knob at a time**, with Plugin Window open and Live on, and watch the pictures follow it. That shows what each control really does: whether a gain knob is in dB, whether a frequency knob lands where its label says.
9. **Press Copy Report** and paste the text wherever you keep notes, together with the plugin's name, its settings and the test level.

To compare two plugins, measure both the same way, with the same level and tests, and compare their reports. To check a change to a plugin, measure it before and after with the same settings.

::: tip
A plugin that measures well can still sound wrong, and the other way round. After measuring, listen to it on music, as you would normally: play the file in the editor with the plugin on in the Master Section.
:::

## From the command line {#analyze-plugin-command}

[]{.ix t="Plugin Analyzer!command line"}`phi --analyze-plugin "Φ EQ"` runs the same measurements without opening a window and prints the report, for scripts and for checking many plugins in a row. `phi --analyze-plugin --list` lists every plugin PHI knows. The options:

| Option | Does |
|---|---|
| `--set "Parameter=value"` | Sets a parameter by its name, to a value as its text; repeat for more |
| `--level dB` | The test level (−12) |
| `--sine Hz` | The harmonics test's tone (1000 Hz); only here, not in the window |
| `--sweep seconds`, `--tail seconds` | The sweep (2 s) and how long the plugin may ring (1.5 s) |
| `--modulation seconds` | How long the modulation test listens (12 s) |
| `--rate Hz` | The sample rate (48000) |
| `--skip linear,harmonics,aliasing,dynamics,modulation` | Leaves those tests out |
