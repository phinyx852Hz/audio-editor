# Metadata

[]{.ix t="Metadata"}[]{.ix t="Tags"}File > Metadata (`Ctrl+Alt+M`) edits everything a file says about itself: its title and artist, its cover art, its broadcast and loudness information, its copyright. It is modelled on Audition's Metadata panel, and every edit is undoable.

![The Metadata window: the Common tab, and a tab for each schema.](images/metadata.png)

## The Common tab

The **Common** tab holds the fields every format shares: title, artist, album, album artist, track, disc, date, genre, comment, composer, lyricist, conductor, subtitle, grouping, copyright, publisher, ISRC, BPM, key, language, lyrics, URL and more. Every schema follows it.

## A tab per schema

[]{.ix t="XMP"}[]{.ix t="Broadcast Wave"}[]{.ix t="ID3"}[]{.ix t="iXML"}

| Tab | Holds |
|---|---|
| XMP | All of Adobe's schemas: Dublin Core, Basic, Rights, Media Management, Job Ticket, Dynamic Media, PDF, Photoshop, IPTC Core and Extension, Camera Raw, EXIF, Script; searchable, schemas can be hidden, and Add Property makes custom namespaces |
| Broadcast Wave | bext version 2, with UMID and the loudness fields; **Fill In Now** fills them from the file |
| ID3 | MP3 tags |
| RIFF INFO | WAV's own tags |
| Cart | AES46 radio cart chunk |
| iXML | Production sound metadata (iXML 2.0): scene, take and take type, speed and timecode, tracks, file set, location, the AFSI user fields, and a LOUDNESS copy of the bext loudness |
| Vorbis comments | FLAC and Ogg tags |
| MP4 atoms | M4A and MP4 tags |
| APEv2 | WavPack and APE tags |
| Pictures | Cover art and other pictures |
| Raw | Every chunk, frame or atom in the file |

A field left empty follows the Common value, shown greyed. Typing gives that schema its own value, listed as a conflict until you **Sync**.

**Raw** lists everything in the file. Chunks PHI does not know are kept and written back unless you delete them, so nothing another program stored is lost.

## Writing

**Write** chooses which schemas are saved for each file type, the ID3 version, and **Strip All** for a clean file. Tags are read when a file opens and written when you save: WAV and AIFF by PHI itself, other formats with TagLib, and XMP in a `.xmp` file beside formats that cannot hold it.

## Templates, import and export

[]{.ix t="Metadata!templates"}**Templates** with `{variables}` fill many fields in one go. Tags import and export as CSV or JSON.
