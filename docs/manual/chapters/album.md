# Albums

[]{.ix t="Album"}The Album menu (`Alt+B`) puts an album together, in the spirit of WaveLab's Audio Montage: songs in order with pauses or crossfades between them, each with its own title, ISRC and fades, played through the Master Section and written as a CD image or a set of gapless files.

![An album of four songs: blocks above the waveform, and track regions.](images/album.png)

## Building an album

| Keys | Does |
|---|---|
| `Ctrl+Alt+Shift+M` | New album |
| `Alt+Insert` | Adds songs (or open them from the Find panel while the album is in front) |
| `Alt+Left` / `Alt+Right` | Previous or next song |
| `Alt+Shift+Left` / `Alt+Shift+Right` | Moves the song earlier or later |
| `Alt+Up` / `Alt+Down` | Lengthens or shortens the pause before a song, in half seconds; below zero the songs crossfade, with equal power |
| `Alt+Enter` | Song properties: title, artist, ISRC, gain, fades and the part of the file used |
| `Alt+Shift+Enter` | Album properties: title, artist and UPC/EAN |
| `Alt+Delete` | Removes the song |
| `Ctrl+Alt+Enter` | Opens the song in its own tab to edit it |
| `Alt+Shift+T` | Switches between one track and two |
| `Ctrl+Alt+Shift+X` | Export Album |

The album plays through the Master Section, with the meters, like any file. Its songs show as blocks above the waveform and as track regions. Songs at another sample rate are converted.

**Editing a song.** `Ctrl+Alt+Enter` opens the song in a tab of its own. Edit it there; the album picks up the edits when you go back to it, saved or not.

[]{.ix t="Album!two tracks"}**One track or two.** With one track, overlapping songs crossfade by themselves. With two tracks, like WaveLab's, songs alternate between them and can overlap freely, each shaped by its own fade in and fade out from Song Properties.

## Saving and exporting

Save keeps a small `.phalbum` file that points to the songs (older `.album` files still open).

[]{.ix t="Export Album"}[]{.ix t="CUE sheet"}[]{.ix t="CD-Text"}**Export Album** (`Ctrl+Alt+Shift+X`) writes:

- the whole album as **one file with a CUE sheet**: track and pregap indexes, ISRC codes, CD-Text titles and the catalogue number, ready to burn or upload; and/or
- a **gapless file per song**, with its tags.

Either can go through the Master Section.
