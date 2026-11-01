<!--
Metadata (owner, 2026-10-31): the term and its place in communication and semiosis, auditory
and music semiotics, embedded metadata, then every schema the Metadata editor (File > Metadata)
reads and writes, each with what it means for music production. The schemas and fields named
here are the ones in Source/core/Metadata.cpp, RiffChunks, Ixml, Xmp, Mp4Xmp and engine/TagIO.
How to use the editor belongs in the manual, not here.
-->

# Metadata

## Metadata

Data about data: the description that travels with a thing and says what it is, who made it,
when, under what terms and how it relates to other things. The Greek *meta* means "after" or
"beyond", and the word names a second order of information that points at a first.

The term was coined by the computer scientist Philip R. Bagley in *Extension of Programming
Language Concepts* (1968), for data that describes other data in a program. The practice is far
older. The clay tablets of Ashurbanipal's library at Nineveh (7th century BC) carried
colophons naming the scribe, the owner and the series a tablet belonged to; the Library of Alexandria's *Pinakes* (Callimachus,
3rd century BC) catalogued its scrolls by author and genre; the card catalogue, Melvil Dewey's
classification (1876) and the Library of Congress's MARC records (Henriette Avram, 1960s) made
description a discipline. The web made it a problem of scale: the Dublin Core was agreed at a
workshop in Dublin, Ohio, in 1995 so that anything online could be described in fifteen common
elements.

Library science distinguishes three kinds: **descriptive** metadata (title, creator, subject),
which lets a thing be found; **structural** metadata (chapters, tracks, markers), which says how
its parts fit together; and **administrative** metadata (rights, provenance, technical format),
which says how it may be used and kept. Every schema in PHI's Metadata editor is a mixture of
the three.

- [Library catalog](https://en.wikipedia.org/wiki/Library_catalog), Wikipedia.
- [MARC standards](https://en.wikipedia.org/wiki/MARC_standards), Wikipedia.
- [Dublin Core](https://en.wikipedia.org/wiki/Dublin_Core), Wikipedia.
- [Metadata](https://en.wikipedia.org/wiki/Metadata), Wikipedia.
- Philip R. Bagley, *Extension of Programming Language Concepts* (University City Science
  Center, Philadelphia, 1968).

- Jeffrey Pomerantz, *Metadata* (MIT Press Essential Knowledge, 2015).

## Metadata, communication and semiosis

Semiosis is the process by which something comes to stand for something else to someone: a
sign, its object and its interpretation, in Charles Sanders Peirce's triad. Metadata is semiosis
made explicit. A file's title tag is a symbol (a sign by convention, in Peirce's terms) that
names the sound; its timestamp is an index (a sign by causal connection) of the moment it was
made; its waveform thumbnail or cover art is an icon (a sign by resemblance). Without such signs
a recording is a string of numbers with no author, no context and no use.

Communication theory placed this second level at the centre early on. In Roman Jakobson's
model of communication ("Linguistics and Poetics", 1960) every message has an addresser, an
addressee, a context, a contact and a **code**, and the *metalingual* function is the message
that speaks about the code itself ("what do you mean by...?"). Gregory Bateson and Jürgen Ruesch
called the same thing **metacommunication** (*Communication: The Social Matrix of Psychiatry*,
1951): every message carries a second message about how to take it, and Bateson's "this is play"
(1955) showed that the frame can change the meaning of everything inside it. Claude Shannon's
*A Mathematical Theory of Communication* (1948) had already separated the signal from its
meaning; metadata is where meaning is put back on top of the signal.

Gérard Genette gave the literary name for it: **paratext** (*Seuils*, 1987; *Paratexts:
Thresholds of Interpretation*, 1997), the titles, authors' names, prefaces, dedications and
covers that are not the text and yet decide how it is read. For music the paratext is the title,
the credits, the cover, the liner notes, the label and the genre: the threshold through which a
listener enters a recording. Embedded metadata is that threshold carried inside the file.

This is why metadata matters so much. It is how a work is found (search and catalogues), how it
is understood (context, genre, language, lyrics), how it is attributed and paid (credits,
identifiers, rights), how it is preserved (format, provenance, coding history) and how machines
can act on it (tempo, key, loudness, timecode). A sound whose metadata is lost has lost its name,
its maker and its history.

- [Semiosis](https://en.wikipedia.org/wiki/Semiosis), Wikipedia.
- [Charles Sanders Peirce](https://en.wikipedia.org/wiki/Charles_Sanders_Peirce), Wikipedia.
- [A Mathematical Theory of Communication](https://en.wikipedia.org/wiki/A_Mathematical_Theory_of_Communication), Wikipedia.
- [Semiotics](https://en.wikipedia.org/wiki/Semiotics), Wikipedia.
- [Jakobson's functions of language](https://en.wikipedia.org/wiki/Jakobson%27s_functions_of_language),
  Wikipedia.
- [Metacommunication](https://en.wikipedia.org/wiki/Metacommunication), Wikipedia.
- [Paratext](https://en.wikipedia.org/wiki/Paratext), Wikipedia.
- Charles Sanders Peirce, *Collected Papers*, vol. 2, *Elements of Logic* (Harvard, 1932).
- Roman Jakobson, "Linguistics and Poetics", in Thomas Sebeok (ed.), *Style in Language* (MIT
  Press, 1960).
- Gregory Bateson, "A Theory of Play and Fantasy" (1955), in *Steps to an Ecology of Mind*
  (Chandler, 1972).
- Gérard Genette, *Paratexts: Thresholds of Interpretation* (Cambridge University Press, 1997).
- Umberto Eco, *A Theory of Semiotics* (Indiana University Press, 1976).

## Metadata in auditory and music semiotics

Music semiotics asks how music means. Jean Molino's and Jean-Jacques Nattiez's **tripartition**
(*Fondements d'une sémiologie de la musique*, 1975; *Music and Discourse*, 1990) separates the
*poietic* level (the making: who composed, performed and recorded, with what intent), the
*neutral* level (the trace itself: the score or the sound) and the *esthesic* level (the
hearing: how listeners construe it). Metadata is the poietic level written down and attached to
the trace: it is how the esthesic listener learns who made the sound and how, which changes
what they hear. Eero Tarasti (*A Theory of Musical Semiotics*, 1994), Kofi Agawu (*Playing with
Signs*, 1991) and Philip Tagg (*Music's Meanings*, 2013) all show how much of musical meaning
lies in conventions, genres and contexts that the sound alone does not state.

Auditory semiotics, the study of meaning in sound in general, gives the same lesson. Pierre
Schaeffer's four modes of listening (*Traité des objets musicaux*, 1966) and Michel Chion's
causal, semantic and reduced listening (*Audio-Vision*, 1990) separate hearing a sound for its
source, for its meaning and for itself. A recording cut from its source is an *acousmatic* sound,
heard without seeing its cause; metadata restores the cause in words: what was recorded, where,
with which microphone, in which scene and take. R. Murray Schafer's *soundmarks* (*The Tuning of
the World*, 1977) and the archives of acoustic ecology depend on the same: a field recording
without its place, date and conditions has no documentary value.

So in music the file's metadata is not an afterthought to the sound. Genre and mood tags place
it in a style; credits and liner notes give it authorship and history; tempo, key and time
signature describe its structure for musicians and software; ISRC and rights fields give it its
legal and economic life. The cover and title are often the first signs a listener meets, before
any sound.

- [Jean-Jacques Nattiez](https://en.wikipedia.org/wiki/Jean-Jacques_Nattiez), Wikipedia.
- [Pierre Schaeffer](https://en.wikipedia.org/wiki/Pierre_Schaeffer), Wikipedia.
- [Soundscape](https://en.wikipedia.org/wiki/Soundscape), Wikipedia.
- [Musical semiotics](https://en.wikipedia.org/wiki/Musical_semiotics), Wikipedia.
- [Acousmatic sound](https://en.wikipedia.org/wiki/Acousmatic_sound), Wikipedia.
- Jean-Jacques Nattiez, *Music and Discourse: Toward a Semiology of Music* (Princeton University
  Press, 1990).
- Eero Tarasti, *A Theory of Musical Semiotics* (Indiana University Press, 1994).
- Kofi Agawu, *Playing with Signs: A Semiotic Interpretation of Classic Music* (Princeton
  University Press, 1991).
- Philip Tagg, *Music's Meanings: A Modern Musicology for Non-Musos* (MMMSP, 2013).
- Pierre Schaeffer, *Traité des objets musicaux* (Seuil, 1966); *Treatise on Musical Objects*,
  translated by Christine North and John Dack (University of California Press, 2017).
- Michel Chion, *Audio-Vision: Sound on Screen* (Columbia University Press, 1994).
- R. Murray Schafer, *The Tuning of the World* (Knopf, 1977).
- Theo van Leeuwen, *Speech, Music, Sound* (Macmillan, 1999).

## Embedded metadata

Metadata can live in three places: in a separate database or catalogue (a library's records, a
distributor's database), in a **sidecar** file beside the media (a .cue sheet, an .xmp file), or
**embedded** in the media file itself. Embedded metadata travels with the file wherever it is
copied, so it survives being emailed, uploaded, archived or found on an old drive without its
catalogue. Its weakness is that every format has its own place and its own vocabulary for it,
and a program that does not know a schema may silently drop it when it saves the file.

Digital audio formats grew their own schemas one after another. Electronic Arts' IFF (1985)
introduced chunks of tagged data, which Apple's AIFF (1988) and Microsoft and IBM's RIFF WAV
(1991) took over with text chunks for names and copyrights. Broadcasters added the Broadcast Wave
bext chunk (1997), radio automation the cart chunk (AES46, 2002) and film sound iXML (2004). MP3
had no place for tags at all until Eric Kemp's ID3 (1996), followed by ID3v2 (1998). Xiph's
Vorbis comments (2000) served Ogg and later FLAC, Apple's atoms MP4 and AAC, APE tags Monkey's
Audio and WavPack. Adobe's XMP (2001) tried to give every file type one common, extensible
vocabulary built on the W3C's RDF.

PHI's Metadata editor (File > Metadata) reads and writes all of these. It keeps a set of
**common fields** (title, artist, album, track, date, genre, comment, composer, ISRC...) that
every schema follows, so a title typed once is written as RIFF INAM, ID3 TIT2, Vorbis TITLE, MP4
©nam, APE Title and XMP dc:title together. Where a file's schemas disagree (an ID3 title that
differs from its RIFF title) both are kept and shown as a conflict until they are synced; a
field that only one schema has is kept as that schema's own. Chunks and frames PHI does not know
are kept as they were and written back, so saving in PHI does not strip what another program put
there.

- [Tag (metadata)](https://en.wikipedia.org/wiki/Tag_(metadata)), Wikipedia.
- [Resource Interchange File Format](https://en.wikipedia.org/wiki/Resource_Interchange_File_Format), Wikipedia.
- [Sidecar file](https://en.wikipedia.org/wiki/Sidecar_file), Wikipedia.
- [Interchange File Format](https://en.wikipedia.org/wiki/Interchange_File_Format), Wikipedia.
- [Embedded Metadata Manifesto](https://www.embeddedmetadata.org/), IPTC.
- Library of Congress, [Sustainability of Digital Formats](https://www.loc.gov/preservation/digital/formats/),
  with a page on each audio format and its metadata.
- [Federal Agencies Digital Guidelines Initiative](https://www.digitizationguidelines.gov/), the US
  archives' guidelines for BWF and its fields.

## Metadata for music production

Which schemas matter depends on where the audio goes.

**Releases and streaming.** The distributor's database, not the file, is what the stores read:
the release's UPC or EAN barcode, each track's **ISRC**, the artists, writers, publishers and
splits, delivered as DDEX messages. Embedded tags still matter for the copies that circulate as
files (promos, downloads, DJ pools, Bandcamp) and as the source the distributor's data is typed
from. For downloads the essential schemas are **ID3v2** (MP3), **Vorbis comments** (FLAC, Ogg,
Opus) and **MP4 atoms** (AAC, ALAC), with the common fields, cover art, ISRC, barcode, label and
copyright.

**Mastering and CD.** A master for CD carries its ISRC per track and the disc's UPC/EAN as
CD-Text and subcode, delivered as a DDP image or a CUE sheet with the WAV; PHI's album writes the
CUE sheet with CD-Text and ISRCs. Loudness measurements belong in **Broadcast Wave** version 2
(integrated loudness, loudness range, true peak), and **ReplayGain** in Vorbis comments.

**Studio and archive.** **Broadcast Wave** (bext) is the professional WAV standard: its
description, originator, date and time reference survive every DAW and archive, and its coding
history records every conversion. **iXML** carries production sound: project, scene, take, track
names and timecode, which post-production needs to conform. **XMP** carries Adobe's markers,
history and rights and is what Audition and Premiere exchange; its **Dynamic Media** schema
holds tempo, key, time signature, beats and loop flags for sample libraries.

**Broadcast and radio.** The **cart** chunk gives automation systems the cut ID, start and end
dates, intro and segue timers and out cue.

The credits deserve the most care. Unattributed recordings are a lasting problem of the
streaming era: royalties that cannot be matched to a writer or performer go into "black box"
pools, and in the United States the Music Modernization Act (2018) set up the Mechanical
Licensing Collective largely to match them. Most of that matching is done with identifiers and
names that were, or should have been, in the metadata from the start.

- [Music Modernization Act](https://en.wikipedia.org/wiki/Music_Modernization_Act), Wikipedia.
- [Mechanical Licensing Collective](https://en.wikipedia.org/wiki/Mechanical_Licensing_Collective), Wikipedia.
- [Liner notes](https://en.wikipedia.org/wiki/Liner_notes), Wikipedia.
- [Album cover](https://en.wikipedia.org/wiki/Album_cover), Wikipedia.
- Recording Academy Producers & Engineers Wing, *Recommendations for Delivery of Recorded Music
  Projects*.
- [DDEX](https://ddex.net/), Digital Data Exchange.
- [The MLC](https://www.themlc.com/), Mechanical Licensing Collective.
- [ReplayGain](https://en.wikipedia.org/wiki/ReplayGain), Wikipedia.
- [Loudness](research:Loudness), in this menu.

## Identifiers: ISRC, ISWC, UPC and ISNI

Names are ambiguous; identifiers are not. Music has a standard code for each thing a royalty can
be paid on.

- **ISRC** (International Standard Recording Code, ISO 3901, 1986): one per *recording*, twelve
  characters (country, registrant, year, number: `GB-ABC-26-00012`). The IFPI is its
  international agency and national agencies issue registrant codes. It is written in ID3 TSRC,
  Vorbis ISRC, MP4 ISRC, APE ISRC, CD subcode and CD-Text.
- **ISWC** (International Standard Musical Work Code, ISO 15707, 2001): one per *composition*,
  issued through CISAC's societies; a song has one ISWC and many ISRCs, one for each recording.
- **UPC / EAN** (GS1's GTIN): one per *release*, the barcode; on CD it is the Media Catalog
  Number. PHI writes it as ID3 TXXX:BARCODE, Vorbis BARCODE, MP4 BARCODE and APE Barcode.
- **ISNI** (ISO 27729, 2012) and **IPI**: one per *person or organization*, so that two artists
  of the same name are not confused and a writer is paid by their collecting society.

- [International Standard Recording Code](https://en.wikipedia.org/wiki/International_Standard_Recording_Code),
  Wikipedia; [ISRC handbook](https://isrc.ifpi.org/), IFPI.
- [ISWC](https://www.iswc.org/), CISAC.
- [ISNI](https://isni.org/).
- [GS1 barcodes](https://www.gs1.org/standards/barcodes).
- [International Standard Musical Work Code](https://en.wikipedia.org/wiki/International_Standard_Musical_Work_Code), Wikipedia.
- [Universal Product Code](https://en.wikipedia.org/wiki/Universal_Product_Code), Wikipedia.
- [International Article Number](https://en.wikipedia.org/wiki/International_Article_Number), Wikipedia.
- [International Standard Name Identifier](https://en.wikipedia.org/wiki/International_Standard_Name_Identifier), Wikipedia.
- [Interested Parties Information](https://en.wikipedia.org/wiki/Interested_Parties_Information), Wikipedia.

## CD-Text, CUE sheets and DDP

The audio CD (Philips and Sony's Red Book, 1980) carried almost no metadata: track starts and
the subcode's Q channel, which can hold each track's ISRC and the disc's Media Catalog Number.
**CD-Text** (Sony, 1996) added titles and performers in the lead-in. The **CUE sheet**, a plain
text file from Jeff Arnold's CDRWIN in the late 1990s, describes a disc's tracks, pauses, ISRCs
and CD-Text beside a single audio file, and is still the common sidecar for album masters. The
**DDP** (Disc Description Protocol, Doug Carson & Associates) is the image factories press from.

- [Compact Disc Digital Audio](https://en.wikipedia.org/wiki/Compact_Disc_Digital_Audio), Wikipedia.
- [CD-Text](https://en.wikipedia.org/wiki/CD-Text), Wikipedia.
- [Cue sheet](https://en.wikipedia.org/wiki/Cue_sheet_(computing)), Wikipedia.
- [Disc Description Protocol](https://en.wikipedia.org/wiki/Disc_Description_Protocol), Wikipedia.

## DDEX and the supply chain

DDEX (Digital Data Exchange), founded in 2006 by labels, collecting societies and digital
services, standardizes the XML messages a distributor sends to the stores: the Electronic
Release Notification (ERN) with the release, its tracks, artists, contributors and their roles,
rights and territories, and further messages for sales reports and works. It is not embedded in
the audio file, but it is where most of the metadata a listener sees on a streaming service
comes from, and the embedded tags of a master are often its first source. Its role vocabulary
(producer, mixing engineer, mastering engineer...) is the model for full studio credits.

- [DDEX](https://en.wikipedia.org/wiki/DDEX), Wikipedia.
- [DDEX standards](https://ddex.net/standards/).
- [Electronic Release Notification](https://kb.ddex.net/), DDEX knowledge base.

# Metadata / Schemas

## RIFF INFO and DISP

The **INFO** list of a WAV file: short text chunks with four-letter IDs (INAM name, IART artist,
IPRD product or album, ICRD creation date, ICMT comments, ICOP copyright, IGNR genre, ISFT
software, IENG engineer, IMUS composer, IWRI writer and others), and the **DISP** chunk with a
display title.

Defined by Microsoft and IBM in the *Multimedia Programming Interface and Data Specifications
1.0* (1991), which made the RIFF container for WAV and AVI, after Electronic Arts' IFF of 1985.
Its fields were meant for any multimedia file, so some (ISHP sharpness, IDPI dots per inch,
ICRP cropped, IPLT palette) only make sense for images; PHI shows them all. RIFF's 4 GB limit
was lifted by RF64 (EBU Tech 3306, 2007), which PHI reads.

For music production: the most widely read tags in WAV. Almost every DAW, sampler and file
browser shows INAM, IART and ICMT, so they are worth filling on every WAV delivered. The text is
plain bytes with no declared encoding; PHI reads text that is not UTF-8 as Latin-1.

- [Resource Interchange File Format](https://en.wikipedia.org/wiki/Resource_Interchange_File_Format),
  Wikipedia.
- [RIFF INFO tags](https://exiftool.org/TagNames/RIFF.html#Info), ExifTool's list.

## Broadcast Wave (bext)

The **bext** chunk of a Broadcast Wave file (BWF): a fixed-size description (256 characters),
originator (32) and originator reference (32), origination date and time, the **time reference**
(the first sample's position in samples since midnight, which places the file on a timeline),
a SMPTE **UMID** (a unique material identifier), five **loudness** values (integrated loudness,
loudness range, maximum true peak, maximum momentary and short-term loudness) and the **coding
history**, one line per conversion the audio has been through.

The European Broadcasting Union published it as EBU Tech 3285 in 1997; version 1 (2001) added
the UMID and version 2 (2011) the loudness fields after EBU R 128. AES31 made it the file format
for exchanging projects between DAWs. PHI writes version 2 and fills the loudness from its own
measurement.

For music production: the professional WAV. The time reference keeps stems and takes in sync
across DAWs, the description and originator survive every archive, and the loudness fields say
how a master measured without anyone having to measure it again.

- [Broadcast Wave Format](https://en.wikipedia.org/wiki/Broadcast_Wave_Format), Wikipedia.
- [EBU Tech 3285](https://tech.ebu.ch/publications/tech3285), version 2 (2011).
- [EBU R 98](https://tech.ebu.ch/publications/r098), the format of the coding history.
- [BWF MetaEdit](https://mediaarea.net/BWFMetaEdit), the US archives' tool for checking it.

## Cart chunk (AES46)

The **cart** chunk: the label of a radio "cart". Version, title, artist, cut ID, client ID,
category, classification, out cue, start and end dates and times (when the spot may air),
producer application and version, a user field, the level reference, a URL, tag text, and
**timers** in samples (SEG segue points, INT intro, AUD audio start and end and others) for the
automation to fade and talk over.

Named after the NAB tape cartridges radio stations played spots and songs from from the 1960s
on. A group of radio automation makers drew it up as CartChunk around 2000, and the Audio
Engineering Society standardized it as AES46-2002. Its fields have fixed lengths, which PHI's
editor enforces.

For music production: what a radio station needs from a delivered single or advertisement. The
intro and segue timers let DJs and automation talk up to the vocal and crossfade at the right
point.

- [Fidelipac](https://en.wikipedia.org/wiki/Fidelipac), Wikipedia. (the NAB cartridge)
- [Radio automation](https://en.wikipedia.org/wiki/Radio_automation), Wikipedia.
- [Broadcast Wave Format](https://en.wikipedia.org/wiki/Broadcast_Wave_Format), Wikipedia.
- AES46-2002, *Radio traffic audio delivery extension to the broadcast-WAVE-file format*,
  [Audio Engineering Society standards](https://www.aes.org/publications/standards/).

## iXML

An XML chunk in WAV files for **production sound**: project, scene, take, tape, the circled, no
good, false start and wild track flags, notes, speed and **timecode** (rate, drop or non-drop
frame, sample rate, the timestamp since midnight), the track list with each track's name and
function, the file set of a multitrack recording, history (original and parent files), location
(name, GPS, altitude) and user fields.

Proposed in 2004 by a group of field recorder and post-production software makers, coordinated
by Gallery Software, so that what a sound recordist types on set reaches the editor. It
is open and extensible: PHI keeps every element by its path, so fields it has no name for survive.

For music production: essential for film and television sound, location and session
recordings, where track names and timecode must reach the editing room. In a music studio it is
a good place for take and scene notes of a multitrack session.

- [iXML specification](http://www.gallery.co.uk/ixml/).
- [iXML](https://en.wikipedia.org/wiki/IXML), Wikipedia.

## AIFF text chunks

AIFF's own text chunks: **NAME** (title), **AUTH** (author), **(c) ** (copyright) and **ANNO**
(annotation). AIFF files also carry an **ID3** chunk, which iTunes and most DAWs read for the
full set of tags, and XMP in an APPL chunk with the signature "XMP ".

Apple defined AIFF in 1988 from Electronic Arts' IFF, with the text chunks from the start; AIFF-C
(1991) added compression. PHI writes the text chunks, the ID3 chunk and XMP together.

For music production: the native uncompressed format on the Mac. Its own chunks are few, so the
ID3 chunk does most of the work.

- [Audio Interchange File Format](https://en.wikipedia.org/wiki/Audio_Interchange_File_Format),
  Wikipedia.

## ID3v1 and ID3v2

The tags of MP3 files, and an ID3 chunk in WAV and AIFF. **ID3v1** is 128 bytes at the end of the
file: title, artist and album of 30 characters, year, comment, a track number (v1.1) and a genre
from a numbered list. **ID3v2** is a tag at the start of the file made of **frames** with
four-letter IDs: TIT2 title, TPE1 artist, TPE2 album artist, TALB album, TRCK track, TPOS disc,
TDRC recording date, TCON genre, COMM comment, TCOM composer, TEXT lyricist, USLT lyrics, TCOP
copyright, TPUB publisher, TSRC ISRC, TBPM tempo, TKEY key, sort orders, original release
fields, the W frames for web pages, TXXX user frames (PHI's barcode and catalogue number),
**APIC** pictures (cover art), **CHAP** and **CTOC** chapters, and **PRIV** private frames, where
PHI keeps an MP3's XMP.

Eric Kemp wrote ID3 in 1996 for his program Studio3; Michael Mutschler added the track number in
1997 (v1.1). Martin Nilsson and others designed ID3v2 in 1998; version 2.3 (1999) is still the
most widely read, and 2.4 (2000) added UTF-8 and new date frames. The chapter frames came in 2005
for podcasts and audiobooks. PHI writes either 2.3 or 2.4, with v1 as well if wanted, and writes
the document's markers as chapters.

For music production: the tags every MP3 promo, download and DJ copy carries. Fill the common
fields, ISRC, label, copyright and cover art; choose v2.3 for the widest compatibility.

- [ID3](https://en.wikipedia.org/wiki/ID3), Wikipedia.
- [id3.org](https://id3.org/), the specifications: ID3v2.3.0, ID3v2.4.0 and the chapter addendum.

## Vorbis comments

The tags of FLAC, Ogg Vorbis, Opus and Speex: plain `FIELD=value` lines in UTF-8, with any field
name and as many values as wanted. The recommended ones include TITLE, VERSION, ARTIST,
ALBUMARTIST, ALBUM, TRACKNUMBER and TRACKTOTAL, DISCNUMBER and DISCTOTAL, DATE, GENRE, COMMENT,
DESCRIPTION, PERFORMER, COMPOSER, LYRICIST, CONDUCTOR, COPYRIGHT, LICENSE, ORGANIZATION (the
label), ISRC, BARCODE, CATALOGNUMBER, BPM, INITIALKEY and the four **REPLAYGAIN** values. Cover art
goes in a FLAC PICTURE block (or METADATA_BLOCK_PICTURE in Ogg).

Xiph.Org defined them with the Vorbis codec in 2000; Josh Coalson's FLAC (2001) adopted them, and
Ogg Opus (RFC 7845, 2016) too. ReplayGain, which David Robinson proposed in 2001, stores a track's
and an album's gain and peak so players can level them.

For music production: the tags of lossless downloads and archives. Being free text they hold
every credit a label wants; the hi-res stores read them.

- [Vorbis comment](https://en.wikipedia.org/wiki/Vorbis_comment), Wikipedia.
- [Ogg Vorbis I format specification: comment field and header](https://xiph.org/vorbis/doc/v-comment.html),
  Xiph.Org.
- [FLAC format](https://xiph.org/flac/format.html), Xiph.Org.

## MP4 and iTunes atoms

The tags of M4A, AAC and ALAC files: **atoms** in the file's `moov/udta/meta/ilst` box, with
four-character names, many beginning with ©: ©nam title, ©ART artist, aART album artist, ©alb
album, trkn track, disk disc, ©day date, ©gen genre, ©cmt comment, ©wrt composer, ©lyr lyrics,
cprt copyright, tmpo tempo, ©grp grouping, ©too encoder, cpil compilation, **covr** cover art, and
freeform `----:com.apple.iTunes:` atoms for ISRC, label, barcode, key, lyricist, conductor,
subtitle and language. XMP goes in Adobe's top-level uuid box.

The MP4 container is ISO/IEC 14496-14 (2003), built on Apple's QuickTime format (1991). The
iTunes atoms are Apple's, not ISO's, from iTunes (2001) and the iTunes Store (2003); they became
the de facto standard because every player follows Apple.

For music production: the tags of Apple Music deliverables and AAC files. Apple Digital Masters
and the iTunes Store read title, artist, composer, ISRC and cover art from here.

- [MP4 file format](https://en.wikipedia.org/wiki/MP4_file_format), Wikipedia.
- [QuickTime File Format](https://developer.apple.com/documentation/quicktime-file-format), Apple.
- [MP4 tags](https://exiftool.org/TagNames/QuickTime.html#ItemList), ExifTool's list.

## APEv2

The tags of WavPack, Monkey's Audio (APE), Musepack and OptimFROG: items with free text keys
(Title, Subtitle, Artist, Album Artist, Album, Track, Disc, Year, Genre, Comment, Composer,
Lyricist, Conductor, Lyrics, Copyright, Publisher, ISRC, Barcode, BPM, Language, Related URL,
Media, Catalog), UTF-8 values, and binary items such as the cover art, at the end of the file.

APE tags came with Matthew Ashland's Monkey's Audio around 2000; Frank Klemm revised them as
APEv2 for Musepack. They are free form like Vorbis comments but binary-safe.

For music production: mostly for lossless archives in WavPack, which also stores floating point
and multichannel audio.

- [APE tag](https://en.wikipedia.org/wiki/APE_tag), Wikipedia.
- [APEv2 specification](https://wiki.hydrogenaud.io/index.php?title=APEv2_specification),
  Hydrogenaudio.

## XMP

The Extensible Metadata Platform: metadata as an RDF/XML **packet** that any file can carry,
divided into **namespaces** (schemas), each with its own prefix (dc:, xmp:, xmpDM:...). Values
are simple text, language alternatives (one per language, with x-default), unordered bags or
ordered sequences, and structures. Anyone can define a namespace, so it holds what no other
schema does.

Adobe introduced XMP in 2001 with Acrobat 5 and made it the metadata of all its applications;
it became ISO 16684-1 in 2012. In audio files it lives in the WAV **_PMX** chunk, the AIFF APPL
chunk, the MP4 **uuid** box, an ID3 **PRIV** frame in MP3, or a **sidecar .xmp** beside a file
that cannot hold it. Audition stores its markers and the edit history here (xmpDM:Tracks,
xmpMM:History); PHI keeps such structures unchanged and writes them back.

For music production: the bridge to Adobe's applications, Audition, Premiere and Bridge, and
the one schema that every format can carry. It holds the most fields; the XMP submenu explains
each of its schemas.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform),
  Wikipedia.
- [XMP specification](https://github.com/adobe/xmp-docs), Adobe, parts 1 to 3.
- ISO 16684-1:2019, *Graphic technology: Extensible metadata platform (XMP), Part 1: Data model,
  serialization and core properties*.

# Metadata / XMP schemas

## Dublin Core

The **dc:** namespace: fifteen elements to describe any resource. Title, creator, description,
subject (keywords), rights, publisher, contributor, date, language, format, identifier, source,
relation, coverage and type.

Agreed at the OCLC and NCSA workshop in Dublin, Ohio, in March 1995 so that web documents could
be described without a librarian; it became ISO 15836 (2003) and is kept by the Dublin Core
Metadata Initiative. XMP took it as its first namespace.

For music production: the most portable of the XMP schemas. dc:title, dc:creator, dc:rights and
dc:subject follow PHI's common fields, and every XMP reader understands them.

- [Dublin Core](https://en.wikipedia.org/wiki/Dublin_Core), Wikipedia.
- [DCMI Metadata Terms](https://www.dublincore.org/specifications/dublin-core/dcmi-terms/).

## XMP Basic

The **xmp:** namespace: the file's own life. Create date, modify date, metadata date, creator
tool (the application that made it), label, rating (0 to 5 stars), nickname, identifiers, base
URL and advisory. Part of XMP since 2001.

For music production: the rating and label are how Bridge and Audition sort and colour files,
useful for choosing takes and sounds; the creator tool records the software that wrote the file.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [XMP Basic namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces), Adobe.

## XMP Rights Management

The **xmpRights:** namespace: whether the file is copyrighted (marked), its owners, the usage
terms, a URL for the copyright statement and a certificate. Part of XMP since 2001, and the
place Creative Commons recommended for its licences.

For music production: where to say how a recording, sample or stem may be used, in words and
with a link, beside the plain copyright line.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [Copyright notice](https://en.wikipedia.org/wiki/Copyright_notice), Wikipedia.
- [XMP Rights Management namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces),
  Adobe.
- [Creative Commons: XMP](https://wiki.creativecommons.org/wiki/XMP).

## XMP Media Management

The **xmpMM:** namespace: a file's identity across copies and versions. Document ID (the same for
every version), instance ID (new for each save), original document ID, version ID, rendition
class and parameters, the asset manager, and the preserved file name; also the **history** of
actions and the files a file was derived from, which PHI keeps as they are.

For music production: provenance. The IDs and history let a mix be traced to the session and
files it came from; asset managers and archives rely on them.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [Digital asset management](https://en.wikipedia.org/wiki/Digital_asset_management), Wikipedia.
- [XMP Media Management namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces),
  Adobe.

## XMP Basic Job Ticket

The **xmpBJ:** namespace: references to the jobs (projects, orders) a file belongs to, each with a
name, an ID and a URL. Meant for print and publishing workflows.

For music production: a place to tie a file to a client's job or project number, which few
audio applications read.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [Job Definition Format](https://en.wikipedia.org/wiki/Job_Definition_Format), Wikipedia.
- [XMP Basic Job Ticket namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces),
  Adobe.

## XMP Paged-Text

The **xmpTPg:** namespace: properties of paged documents, the number of pages and the printing
plate names. It is in the editor because Adobe's Metadata panels list every XMP schema.

For music production: not used for audio; it describes documents such as scores or liner notes
in PDF.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [PDF](https://en.wikipedia.org/wiki/PDF), Wikipedia.
- [XMP Paged-Text namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces),
  Adobe.

## XMP Dynamic Media

The **xmpDM:** namespace: Adobe's schema for audio and video. Music fields (artist, album, track
and disc number, genre, composer, engineer, release date, lyrics), musical structure (**tempo**,
**key**, scale type, **time signature**, number of beats, instrument, **loop**), the audio's
format (sample rate, sample type, channel type, compressor, speaker placement, data rate), cue
points (intro time, out cue), the shooting script of film and video (project, scene, shot name,
number, date, day, location and size, take, tape and alternative tape names, good, director,
director of photography, client, camera angle, label, model and move), video properties, and
peak file paths. Audition writes its **markers** as xmpDM:Tracks, which PHI keeps.

Adobe defined it with XMP for Premiere, After Effects, Audition and Soundbooth.

For music production: the richest musical schema of all. Key, tempo, time signature, beats and
loop are what sample and loop libraries need to be found and matched; the engineer and composer
credits follow PHI's common fields.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [Loop (music)](https://en.wikipedia.org/wiki/Loop_(music)), Wikipedia.
- [Adobe Audition](https://en.wikipedia.org/wiki/Adobe_Audition), Wikipedia.
- [XMP Dynamic Media namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces),
  Adobe.

## Adobe PDF schema

The **pdf:** namespace: keywords, producer (the program that made the PDF), PDF version and
whether it has been trapped for print. From Acrobat, which first carried XMP.

For music production: for PDFs such as scores, lyric sheets and liner notes, not audio files.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [PDF](https://en.wikipedia.org/wiki/PDF), Wikipedia.
- [Adobe PDF namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces), Adobe.

## Photoshop schema

The **photoshop:** namespace: the news description fields Photoshop's File Info dialog took from
the press, in XMP. Headline, credit, source, instructions, author's position, caption writer,
category and supplemental categories, city, state or province, country, date created,
transmission reference and urgency. They are the IPTC fields from before IPTC Core.

For music production: credit and source say who supplied a recording and where it came from;
city, country and date created place a field recording.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [Adobe Photoshop](https://en.wikipedia.org/wiki/Adobe_Photoshop), Wikipedia.
- [Photoshop namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces),
  Adobe.

## IPTC Core

The **Iptc4xmpCore:** namespace: the news agencies' description fields. Country code, sublocation,
intellectual genre, scene codes and subject codes from IPTC's controlled vocabularies.

The International Press Telecommunications Council, founded in 1965, published its Information
Interchange Model in 1991 for news photographs; IPTC Core (2005), made with Adobe, carried it
into XMP.

For music production: for news, documentary and archival audio, where the place and subject must
be stated in a standard way.

- [IPTC Photo Metadata Standard](https://iptc.org/standards/photo-metadata/iptc-standard/).
- [IPTC Core](https://en.wikipedia.org/wiki/IPTC_Information_Interchange_Model), Wikipedia.

## IPTC Extension

The **Iptc4xmpExt:** namespace (2008): persons shown, event, organisations named, additional
model information and the **digital source type**. The last says how the media was made: a
recording of the world, a digital creation, a composite, or, since IPTC's vocabulary of 2022 and
2023, **trained algorithmic media**, content generated by AI.

For music production: the digital source type is now how platforms and C2PA content credentials
recognize generated or AI-assisted audio, and a place to state that a recording is genuine.

- [IPTC Information Interchange Model](https://en.wikipedia.org/wiki/IPTC_Information_Interchange_Model), Wikipedia.
- [Synthetic media](https://en.wikipedia.org/wiki/Synthetic_media), Wikipedia.
- [Content Authenticity Initiative](https://en.wikipedia.org/wiki/Content_Authenticity_Initiative), Wikipedia.
- [IPTC Photo Metadata Extension](https://iptc.org/std/photometadata/specification/IPTC-PhotoMetadata),
  IPTC.
- [Digital source type vocabulary](https://cv.iptc.org/newscodes/digitalsourcetype/), IPTC.
- [C2PA](https://c2pa.org/), Coalition for Content Provenance and Authenticity.

## Camera Raw schema

The **crs:** namespace: Adobe Camera Raw's (2003) settings for a photograph's raw file, of which
PHI's editor lists the raw file name and version. For photographs, not audio; it is listed
because Adobe's panels show it.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [Raw image format](https://en.wikipedia.org/wiki/Raw_image_format), Wikipedia.
- [Camera Raw namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces), Adobe.

## EXIF: TIFF properties

The **tiff:** namespace: the descriptive tags of the TIFF image format (Aldus, 1986) in XMP. Make
and model of the device, software, artist, copyright, date and time, image description. For
images; on audio they can name the recorder (make and model) that made a field recording.

- [TIFF namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces), Adobe.
- [TIFF](https://en.wikipedia.org/wiki/TIFF), Wikipedia.

## EXIF properties

The **exif:** namespace: the Exchangeable Image File Format of digital cameras (JEIDA, 1995, now
kept by CIPA and JEITA) in XMP. Of its many tags PHI lists date and time original and digitized,
user comment, and the **GPS** latitude, longitude and altitude. Exif also defines a format for
audio in digital cameras, rarely used.

For music production: the GPS fields locate a field recording in any application that maps
photographs.

- [Exif](https://en.wikipedia.org/wiki/Exif), Wikipedia.
- [EXIF namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces), Adobe.

## Additional EXIF properties

The **aux:** namespace: Adobe's additions to EXIF for what the camera's tags did not cover. The
device's serial number, the lens and the owner. For photographs; on audio they can identify the
recorder and microphone of a recording.

- [Exif](https://en.wikipedia.org/wiki/Exif), Wikipedia.
- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [EXIF auxiliary namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces),
  Adobe.

## XMP Script

The **xmpScript:** namespace: the screenplay a clip belongs to. Action, characters, dialog, scene
setting and scene characteristics. Adobe added it for its film and video tools, to link footage
and recordings to the script.

For music production: for dialogue, voice-over and audio drama, where each recording answers a
line of a script.

- [Extensible Metadata Platform](https://en.wikipedia.org/wiki/Extensible_Metadata_Platform), Wikipedia.
- [Screenplay](https://en.wikipedia.org/wiki/Screenplay), Wikipedia.
- [XMP Script namespace](https://github.com/adobe/xmp-docs/tree/master/XMPNamespaces),
  Adobe.
