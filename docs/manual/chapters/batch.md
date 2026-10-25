# The Batch Processor

[]{.ix t="Batch Processor"}Tools > Batch Processor (`Ctrl+B`) processes many files the same way, like WaveLab's. It runs in the background, on every core, while you keep editing.

![The Batch Processor: files, steps and outputs.](images/batch.png)

## Adding files

Add single files, whole folders (with their subfolders and a name filter), or the files open in your tabs, or drop them on the window.

## Steps

Pick the processing steps, in order:

| Step | |
|---|---|
| Normalize Loudness | With a true-peak ceiling |
| Normalize Peak | |
| Gain | |
| True Peak Limiter | |
| Master Section | The chain as it is now |
| Trim Silence | At the start and end |
| Add Silence at Start or End | |
| Fade In and Out | |
| Remove DC Offset | |
| Invert | |
| Mix to Mono, Make Stereo | |
| Change Sample Rate | |
| DeClick, DeClip, DeHum, DeEss | |

[]{.ix t="Batch Processor!conditions"}**Conditions.** Any step can run only when a condition holds, for example "only if the true peak is above −1 dBTP" or "only if the sample rate is not 44100 Hz". So a batch can fix only the files that need fixing.

**Presets** start a streaming master, a podcast or a clean-up.

## Outputs and other jobs

The outputs are the same as [Export](#export)'s: several formats per file, names, folders and tags. Other jobs:

- **Write Tags Only**: a tag template with `{variables}`, including `{lufs}`. Lossy files are not encoded again.
- **Rename Files** from their tags, such as `{artist} - {title}`.
- **Measure Only**: no files are written, only measured.

[]{.ix t="Batch Processor!report"}Every job can write a **CSV report** with loudness, loudness range, peaks before and after, clipping, and what was written.

## Running

Files run in parallel on every core, or as many at once as you choose. Double-click a result to open it. Batches save as `.phbatch` files, and the last one comes back when you open the window.
