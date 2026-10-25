# Export

[]{.ix t="Export"}File > Export (`Ctrl+Alt+X`) writes a list of outputs in one go, like WaveLab's rendering: for example a 24-bit WAV for the archive, an MP3 at 320 kbit/s for a preview and an AAC at 256 kbit/s for a store, all from one press.

![Export with three outputs: WAV, MP3 and AAC.](images/export.png)

## What to export

Export the **whole file**, the **selection**, **each region**, or **each part between markers**, and choose whether it goes **through the Master Section**.

## Outputs

[]{.ix t="Export!formats"}[]{.ix t="FFmpeg"}Each output in the list has its own:

- **format and encoder settings**. Every format FFmpeg can encode on your system is offered: MP3, AAC/M4A, Opus, Ogg Vorbis, FLAC, ALAC, WavPack, TTA, AC-3, E-AC-3, DTS, TrueHD, MP2, WMA, Speex, AMR, GSM, G.711, G.722, G.726, ADPCM, aptX, SBC, Matroska and WebM, W64, CAF, AU and more. Formats your FFmpeg lacks are listed greyed out;
- **sample rate, bit depth and dither**;
- **loudness target** (streaming, Apple, podcast and broadcast presets) with a **true-peak ceiling**;
- **folder** and **file name pattern**;
- what to do **when the file exists**;
- **metadata template** and the tag **schemas** to write.

### File name patterns

[]{.ix t="File name patterns"}A pattern builds each file's name from variables in braces:

| Variable | Becomes |
|---|---|
| `{filename}` | The source file's name |
| `{title}`, `{artist}`, `{track}` | Its tags |
| `{part}` | The region's or part's name or number |
| `{rate}`, `{bits}` | The output's sample rate and bit depth |
| `{date}` | Today's date |

For example `{artist} - {title} ({rate})` gives *Ada - Morning (48000).mp3*.

## Preview

[]{.ix t="Export!preview"}Before you export, **Preview** plays the selection (or 20 seconds from the cursor) encoded with the chosen output, in place of your audio, looping. `A` switches between the original and the encoding. **Only what the codec removes** plays the difference, so you can hear exactly what a lossy format throws away.

## After writing

Lossy files are decoded again after they are written, and their true peak checked, since encoding can push peaks up. A summary lists every file with its loudness and peaks.

Output lists are remembered, and can be saved as presets for next time.

::: tip
To export many files the same way, use the [Batch Processor](#batch), which shares Export's outputs.
:::
