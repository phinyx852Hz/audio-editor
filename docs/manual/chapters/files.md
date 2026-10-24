# Files, Tabs and Recovery

## Opening files

[]{.ix t="Opening files"}File > Open (`Ctrl+O`) opens one or more audio files, each in a tab of its own. You can also:

- pass a file to PHI on the command line: `phi song.wav`;
- open a file from the Find panel (see [The Find Panel and the Selector](#find));
- drop files on the window from a file manager;
- double-click a PHI project (`.phi`) or album in your file manager.

PHI opens WAV, AIFF, FLAC, Ogg Vorbis and MP3 directly, and every other format your FFmpeg can decode, among them M4A and AAC, Opus, WavPack and ALAC. Markers in WAV files (cue points and regions) and the file's tags come in with the audio.

### Channels and layouts

[]{.ix t="Channels"}[]{.ix t="Surround!opening"}A file can have any number of channels, from mono through stereo to 5.1, 7.1 and 7.1.4. Each lane and meter is labelled with its speaker (L, R, C, LFE, Ls, Rs…), the file details name the layout, and loudness weights the surround channels as ITU-R BS.1770 asks. When a channel count fits more than one layout (six channels can be 5.1 or a 6.0 layout, for example), Spatial > Channel Layout says which it is. See [Surround, Binaural and Ambisonics](#spatial).

## Tabs {#tabs}

[]{.ix t="Tabs"}

| Keys | Does |
|---|---|
| `Ctrl+O` | Opens a file in a new tab |
| `Ctrl+Tab` / `Ctrl+Page Down` | Next tab |
| `Ctrl+Shift+Tab` / `Ctrl+Page Up` | Previous tab |
| `Ctrl+W` | Closes the tab, when the editor has the keyboard |
| `Shift+C` | Copy to New: the selection, or the whole file, in a new untitled tab, markers included |

Each tab keeps its own undo history, selection, zoom and cursor. Cut, copy and paste work between tabs; pasting between files of different layouts puts the channels in the same places (left into left, centre into centre).

::: note
With a panel focused, `Ctrl+W` closes the panel rather than the file. Give the editor the keyboard first (`Ctrl+F6`) to close a tab.
:::

## Saving

[]{.ix t="Saving"}File > Save (`Ctrl+S`) saves the file in its own format; Save As (`Ctrl+Shift+S`) chooses a name, format, bit depth and dither.

| Format | Saved by | Notes |
|---|---|---|
| WAV | PHI | 16, 24 or 32-bit, or 32-bit float; markers, regions and tags kept; surround files carry their speaker layout |
| AIFF | PHI | |
| FLAC | PHI | Lossless, smaller |
| Ogg Vorbis | PHI | |
| MP3, M4A (AAC), Opus, WavPack | FFmpeg | With their encoder settings |

[]{.ix t="Dither!when saving"}Reducing the bit depth (from 32-bit float to 16-bit, say) adds TPDF dither unless you turn it off, so the quietest sounds fade into a gentle hiss instead of breaking up.

Saving a lossy file (an MP3, say) whose audio you did not change rewrites only its tags, so it is not encoded again and loses nothing.

For several formats at once, loudness targets and file-name patterns, use [Export](#export) instead.

## Autosave and crash recovery {#recovery}

[]{.ix t="Autosave"}[]{.ix t="Crash recovery"}[]{.ix t="Recovery"}Every minute, PHI copies each file with unsaved changes, audio, markers and tags, to `~/.cache/phi/recovery`. If PHI closes without you quitting it (a crash or a power cut), it offers those copies back at the next start.

![Recovering unsaved files after PHI closed unexpectedly.](images/recover-files.png)

- **Recover** opens each one as an unsaved file, ready to check and save.
- **Discard** deletes them.

Quitting normally clears the folder. The undo history is not kept, only the audio as it was.

To change how often PHI autosaves, set `autosaveSeconds` under `[recovery]` in `~/.config/phi/settings.ini`; 0 turns autosave off.

Recordings have their own safety copy; see [Recording](#recording).

## Undo and the History panel {#history}

[]{.ix t="Undo"}[]{.ix t="History panel"}Every edit can be undone with `Ctrl+Z` and redone with `Ctrl+Shift+Z` (or `Ctrl+Y`), as far back as the file was opened. The History panel (`Ctrl+H`) lists every step: move to one and press `Enter` to go back (or forward) to it, and `Esc` to return to the waveform. PHI asks before closing a file with unsaved changes.

## Quitting

File > Quit (`Ctrl+Q`). PHI asks about every file with unsaved changes, and keeps the open project (see [Projects](#projects)) to reopen next time.
